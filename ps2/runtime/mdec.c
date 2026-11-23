/* mdec.c — ver mdec.h. */
#include "mdec.h"
#include "ram.h"
#include "trace_fmv.h"
#include "prof.h"

#include <string.h>
#ifdef PS2_DUMP_MDEC
#include <stdio.h>
static unsigned dump_nq;               /* quadros desde o boot (o reset do MDEC não zera) */
#endif

#define ENTRADA_MAX 0x10000u            /* palavras de parâmetro (16 bits) */
#define SAIDA_MAX   (0x20000u)          /* palavras de saída (512 KB) */

static uint32_t entrada[ENTRADA_MAX];
static uint32_t saida[SAIDA_MAX];
static uint32_t sai_ini, sai_fim;       /* fila de saída: [ini, fim) */

static uint32_t cmd;                    /* comando atual */
static uint32_t faltam;                 /* parâmetros que ainda faltam */
static uint32_t recebidos;
static uint32_t controle;               /* bits 29/30 do registrador de controle */

static uint8_t  q_luma[64], q_croma[64];
static int16_t  escala[64];

static uint32_t pend_madr, pend_palavras;   /* DMA1 esperando dados */
unsigned long long g_prof_mdec;             /* V48: ciclos do EE decodificando */
unsigned long g_prof_mdec_n;                /* quadros (comandos de decodificação) */

/* V48: decodificação em andamento (sob demanda, ver dec_continuar) */
static struct {
    int ativo;                          /* ainda há entrada para decodificar */
    uint32_t prof;                      /* 0=4 bits, 1=8, 2=24, 3=15 */
    int sem_sinal;
    uint16_t bit15;
    uint32_t pos, total;                /* em halfwords da entrada */
} dec;

static const uint8_t zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

void ps2_mdec_reset(void)
{
    dec.ativo = 0;                      /* V48: descarta a decodificação pendente */
    cmd = 0; faltam = 0; recebidos = 0;
    sai_ini = sai_fim = 0;
    pend_palavras = 0;
}

/* ---------------- decodificação ---------------- */

static int s10(uint32_t n) { return (int)((n & 0x3FFu) ^ 0x200u) - 0x200; }
static int lim(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

/* V48: a tabela de escala já deslocada (o IDCT usa escala >> 3), calculada
 * quando a tabela chega e não a cada multiplicação. */
static int16_t esc3[64];

/* Lê um bloco RLE (halfwords) a partir de *pos. 0 se os dados acabaram.
 * V48: *col recebe as colunas (bit = índice & 7) que têm coeficiente
 * diferente de zero; o IDCT pula as outras. */
static int rle_bloco(int16_t *blk, const uint8_t *qt, uint32_t *pos, uint32_t total,
                     unsigned *col)
{
    const uint16_t *src = (const uint16_t *)entrada;
    uint32_t n;
    int k, q, val;
    unsigned c = 0;

    do {
        if (*pos >= total) return 0;
        n = src[(*pos)++];
    } while (n == 0xFE00u);
    memset(blk, 0, 64 * sizeof(int16_t));
    q = (int)((n >> 10) & 0x3Fu);
    k = 0;
    val = s10(n) * qt[0];
    for (;;) {
        int i;
        if (q == 0) val = s10(n) * 2;
        val = lim(val, -0x400, 0x3FF);
        i = q ? zigzag[k] : k;
        blk[i] = (int16_t)val;
        if (val) c |= 1u << (i & 7);
        if (*pos >= total) break;
        n = src[(*pos)++];
        k += (int)((n >> 10) & 0x3Fu) + 1;
        if (k > 63) break;
        val = (s10(n) * qt[k] * q + 4) >> 3;
    }
    *col = c;
    return 1;
}

/* IDCT em duas passadas, com as mesmas contas e arredondamentos de antes
 * (psx-spx): tmp[x+y*8] = (soma_z blk[y+z*8] * E[x+z*8] + 0xFFF) >> 13 e
 * depois blk[x+y*8] = (soma_z tmp[y+z*8] * E[x+z*8] + 0xFFF) >> 13, com
 * E = escala >> 3. V48: só entram os termos que podem ser diferentes de zero
 * — na 1a passada, os coeficientes não nulos das colunas de 'col' (uma
 * coluna toda zero dá tmp zero); na 2a, só essas colunas de tmp. Os blocos do
 * vídeo têm em média ~3 colunas e poucos coeficientes, então em vez de 1024
 * multiplicações por bloco ficam ~200. Somar zeros a menos não muda nada
 * (inteiros, sem estouro). */
static void idct(int16_t *blk, unsigned col)
{
    int32_t t1[8][8];                      /* t1[y][x] = tmp[x + y*8] */
    int zc[8], nc = 0, i, y, z;
    if (!col) return;                      /* bloco todo zero: a saída é zero */
    for (i = 0; i < 8; i++)
        if ((col >> i) & 1u) zc[nc++] = i;
    /* Laços de 8 escritos à mão com somas em variáveis locais: o gcc do EE
     * guardava o vetor de somas na pilha e relia a cada multiplicação. */
    for (i = 0; i < nc; i++) {
        int32_t s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, s5 = 0, s6 = 0, s7 = 0;
        y = zc[i];
        for (z = 0; z < 8; z++) {
            int32_t v = blk[y + z * 8];
            const int16_t *e = &esc3[z * 8];
            if (!v) continue;
            s0 += v * e[0]; s1 += v * e[1]; s2 += v * e[2]; s3 += v * e[3];
            s4 += v * e[4]; s5 += v * e[5]; s6 += v * e[6]; s7 += v * e[7];
        }
        t1[y][0] = (s0 + 0xFFF) >> 13; t1[y][1] = (s1 + 0xFFF) >> 13;
        t1[y][2] = (s2 + 0xFFF) >> 13; t1[y][3] = (s3 + 0xFFF) >> 13;
        t1[y][4] = (s4 + 0xFFF) >> 13; t1[y][5] = (s5 + 0xFFF) >> 13;
        t1[y][6] = (s6 + 0xFFF) >> 13; t1[y][7] = (s7 + 0xFFF) >> 13;
    }
    for (y = 0; y < 8; y++) {
        int32_t s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, s5 = 0, s6 = 0, s7 = 0;
        int16_t *o = &blk[y * 8];
        for (i = 0; i < nc; i++) {
            int32_t t;
            const int16_t *e;
            z = zc[i];
            t = t1[z][y];                  /* = tmp[y + z*8] */
            e = &esc3[z * 8];
            if (!t) continue;
            s0 += t * e[0]; s1 += t * e[1]; s2 += t * e[2]; s3 += t * e[3];
            s4 += t * e[4]; s5 += t * e[5]; s6 += t * e[6]; s7 += t * e[7];
        }
        o[0] = (int16_t)((s0 + 0xFFF) >> 13); o[1] = (int16_t)((s1 + 0xFFF) >> 13);
        o[2] = (int16_t)((s2 + 0xFFF) >> 13); o[3] = (int16_t)((s3 + 0xFFF) >> 13);
        o[4] = (int16_t)((s4 + 0xFFF) >> 13); o[5] = (int16_t)((s5 + 0xFFF) >> 13);
        o[6] = (int16_t)((s6 + 0xFFF) >> 13); o[7] = (int16_t)((s7 + 0xFFF) >> 13);
    }
}

static uint8_t mb_rgb[16 * 16 * 3];     /* macrobloco 16x16 em R,G,B */

/* Cr, Cb e os 4 blocos Y -> mb_rgb. V48: a parte da cor de cada amostra de
 * croma (usada por 4 pixels) é calculada uma vez só. */
static void macrobloco_rgb(int16_t blk[6][64], int sem_sinal)
{
    int cr_[64], cg_[64], cb_[64];
    int c, b, x, y;
    for (c = 0; c < 64; c++) {
        int R = blk[0][c], B = blk[1][c];
        cg_[c] = (-352 * B - 731 * R) >> 10;
        cr_[c] = (1436 * R) >> 10;
        cb_[c] = (1815 * B) >> 10;
    }
    for (b = 0; b < 4; b++) {
        const int16_t *yb = blk[2 + b];
        int xx = (b & 1) * 8, yy = (b >> 1) * 8;
        for (y = 0; y < 8; y++) {
            uint8_t *o = &mb_rgb[(xx + (y + yy) * 16) * 3];
            const int *pr = &cr_[(xx >> 1) + ((y + yy) >> 1) * 8];
            const int *pg = &cg_[(xx >> 1) + ((y + yy) >> 1) * 8];
            const int *pb = &cb_[(xx >> 1) + ((y + yy) >> 1) * 8];
            for (x = 0; x < 8; x++, o += 3) {
                int Y = yb[x + y * 8];
                int R = lim(Y + pr[x >> 1], -128, 127);
                int G = lim(Y + pg[x >> 1], -128, 127);
                int B = lim(Y + pb[x >> 1], -128, 127);
                if (sem_sinal) { R ^= 0x80; G ^= 0x80; B ^= 0x80; }
                o[0] = (uint8_t)R; o[1] = (uint8_t)G; o[2] = (uint8_t)B;
            }
        }
    }
}

static void emitir_bytes(const uint8_t *b, uint32_t n)
{
    uint32_t palavras = n / 4u;             /* n é sempre múltiplo de 4 */
    if (palavras > SAIDA_MAX - sai_fim) palavras = SAIDA_MAX - sai_fim;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    memcpy(&saida[sai_fim], b, palavras * 4u);   /* = montar byte a byte (EE e PC) */
    sai_fim += palavras;
#else
    for (; palavras; palavras--, b += 4)
        saida[sai_fim++] = (uint32_t)b[0] | (uint32_t)b[1] << 8 |
                           (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
#endif
}

/*
 * V48: decodificação SOB DEMANDA, como o MDEC de verdade (que para quando a
 * fila de saída enche): o comando de decodificar só guarda a entrada, e os
 * macroblocos são feitos quando o DMA1 (ou a porta de dados) pede a saída.
 * Antes o quadro inteiro (~25 ms no PCSX2) era decodificado dentro da escrita
 * do CHCR do DMA0, sem atender interrupções: o CD esperava e o vídeo e a
 * música da abertura andavam ~30% mais devagar que o PS1. Agora o trabalho
 * vem em pedaços (uma faixa de 16 pixels por pedido do tocador) e as IRQs do
 * CD são atendidas entre eles. A saída é a mesma, na mesma ordem: um comando
 * novo (ou tabela nova) primeiro termina o que falta do anterior.
 */

static void dec_iniciar(void)
{
    /* fila já consumida: recomeça do início; com sobra, move a sobra para o
     * início para a saída nova caber (auditoria V19 1.5) */
    if (sai_ini == sai_fim) sai_ini = sai_fim = 0;
    else if (sai_ini > 0u) {
        memmove(saida, saida + sai_ini, (size_t)(sai_fim - sai_ini) * 4u);
        sai_fim -= sai_ini;
        sai_ini = 0;
    }
    dec.prof = (cmd >> 27) & 3u;
    dec.sem_sinal = !((cmd >> 26) & 1u);
    dec.bit15 = (cmd >> 25) & 1u ? 0x8000u : 0u;
    dec.pos = 0;
    dec.total = recebidos * 2u;
    dec.ativo = 1;
}

/* decodifica até a fila ter 'quer' palavras ou a entrada acabar */
static void dec_continuar(uint32_t quer)
{
    static int16_t blk[6][64];
    unsigned col[6];
    uint32_t t0;
    if (!dec.ativo || sai_fim - sai_ini >= quer) return;
    t0 = prof_agora();
    while (dec.ativo && sai_fim - sai_ini < quer) {
        int i;
        /* fila vazia: volta ao começo (a saída fica pequena e no cache) */
        if (sai_ini == sai_fim) sai_ini = sai_fim = 0;
        else if (sai_ini > 0u && sai_fim > SAIDA_MAX - 256u) {
            memmove(saida, saida + sai_ini, (size_t)(sai_fim - sai_ini) * 4u);
            sai_fim -= sai_ini;
            sai_ini = 0;
        }
        if (dec.prof <= 1u) {           /* monocromático: só blocos Y */
            uint8_t b[64];
            if (!rle_bloco(blk[0], q_luma, &dec.pos, dec.total, &col[0])) { dec.ativo = 0; break; }
            idct(blk[0], col[0]);
            for (i = 0; i < 64; i++) {
                int v = lim(blk[0][i], -128, 127);
                b[i] = (uint8_t)(dec.sem_sinal ? v ^ 0x80 : v);
            }
            if (dec.prof == 1u) emitir_bytes(b, 64);
            else {
                uint8_t p[32];
                for (i = 0; i < 32; i++) p[i] = (uint8_t)((b[2 * i] >> 4) | (b[2 * i + 1] & 0xF0u));
                emitir_bytes(p, 32);
            }
            continue;
        }
        if (!rle_bloco(blk[0], q_croma, &dec.pos, dec.total, &col[0]) ||     /* Cr */
            !rle_bloco(blk[1], q_croma, &dec.pos, dec.total, &col[1])) {     /* Cb */
            dec.ativo = 0;
            break;
        }
        for (i = 2; i < 6; i++)
            if (!rle_bloco(blk[i], q_luma, &dec.pos, dec.total, &col[i])) break;
        if (i < 6) { dec.ativo = 0; break; }        /* macrobloco incompleto: descarta */
        for (i = 0; i < 6; i++) idct(blk[i], col[i]);
        macrobloco_rgb(blk, dec.sem_sinal);
        if (dec.prof == 2u) emitir_bytes(mb_rgb, sizeof(mb_rgb));
        else {
            uint8_t p[16 * 16 * 2];
            for (i = 0; i < 256; i++) {
                const uint8_t *c = &mb_rgb[i * 3];
                uint16_t v = (uint16_t)((c[0] >> 3) | (c[1] >> 3) << 5 | (c[2] >> 3) << 10 | dec.bit15);
                p[2 * i] = (uint8_t)v; p[2 * i + 1] = (uint8_t)(v >> 8);
            }
            emitir_bytes(p, sizeof(p));
        }
    }
    g_prof_mdec += (uint32_t)(prof_agora() - t0);
}

static void executar(void)
{
    uint32_t c = cmd >> 29;
    uint32_t i;
    const uint8_t *b = (const uint8_t *)entrada;
    if (c == 1u) {
        uint32_t t0;
#ifdef PS2_DUMP_MDEC
        {   /* despeja alguns quadros de entrada para medir no PC (fora da
             * medição de tempo do MDEC; compilar com -DPS2_DUMP_MDEC) */
            unsigned nq = ++dump_nq;
            if (nq == 40u || nq == 150u || nq == 300u || nq == 450u) {
                uint32_t k;
                printf("mdecdump %u %08lx %lu\n", nq, (unsigned long)cmd, (unsigned long)recebidos);
                printf("mdecq");
                for (k = 0; k < 64u; k++) printf(" %u", q_luma[k]);
                for (k = 0; k < 64u; k++) printf(" %u", q_croma[k]);
                printf("\nmdecs");
                for (k = 0; k < 64u; k++) printf(" %d", escala[k]);
                printf("\n");
                for (k = 0; k < recebidos; k += 8u) {
                    uint32_t j;
                    printf("mdecd");
                    for (j = k; j < k + 8u && j < recebidos; j++) printf(" %08lx", (unsigned long)entrada[j]);
                    printf("\n");
                }
            }
        }
#endif
        t0 = prof_agora();
        dec_iniciar();                  /* a decodificação vem sob demanda */
        g_prof_mdec += (uint32_t)(prof_agora() - t0);
        g_prof_mdec_n++;
    }
    else if (c == 2u) {
        for (i = 0; i < 64; i++) q_luma[i] = b[i];
        if (cmd & 1u) for (i = 0; i < 64; i++) q_croma[i] = b[64 + i];
    } else if (c == 3u) {
        const uint16_t *h = (const uint16_t *)entrada;
        for (i = 0; i < 64; i++) { escala[i] = (int16_t)h[i]; esc3[i] = (int16_t)(escala[i] >> 3); }
    }
    TR("mdec exec %08x rec %u saida %u..%u", cmd, recebidos, sai_ini, sai_fim);
    /* DMA1 esperando? */
    if (pend_palavras) {
        uint32_t faltavam = pend_palavras;
        if (ps2_mdec_dma_out(pend_madr, faltavam)) ps2_dma_mdec_saida_pronta(faltavam);
    }
}

void ps2_mdec_write_data(uint32_t v)
{
    if (faltam) {
        if (recebidos < ENTRADA_MAX) entrada[recebidos++] = v;
        if (--faltam == 0) executar();
        return;
    }
    TR_LIGAR();
    /* comando novo: termina o que falta do anterior antes de trocar a
     * entrada ou as tabelas */
    if (dec.ativo) dec_continuar(0xFFFFFFFFu);
    cmd = v;
    recebidos = 0;
    switch (v >> 29) {
    case 1: faltam = v & 0xFFFFu; break;
    case 2: faltam = (v & 1u) ? 32u : 16u; break;
    case 3: faltam = 32u; break;
    default: faltam = 0; break;
    }
    TR("mdec cmd %08x faltam %u", v, faltam);
    if (faltam == 0 && (v >> 29) == 1u) executar();
}

uint32_t ps2_mdec_read_data(void)
{
    TR_CONTA(g_tr_mdec_rd);
    if (sai_ini >= sai_fim) dec_continuar(1u);
    if (sai_ini < sai_fim) return saida[sai_ini++];
    return 0;
}

uint32_t ps2_mdec_read_status(void)
{
    uint32_t s = 0;
    TR_CONTA(g_tr_mdec_st);
    if (sai_ini >= sai_fim) dec_continuar(1u);             /* V48: vazia de verdade? */
    if (sai_ini >= sai_fim) s |= 1u << 31;                 /* saída vazia */
    if (faltam) s |= 1u << 29;                             /* comando em andamento */
    if ((controle & (1u << 30)) && faltam) s |= 1u << 28;  /* pede entrada */
    if ((controle & (1u << 29)) && sai_ini < sai_fim) s |= 1u << 27;
    s |= ((cmd >> 25) & 0xFu) << 23;                       /* formato */
    if (!faltam) s |= 4u << 16;                            /* bloco atual (ocioso) */
    s |= faltam ? ((faltam - 1u) & 0xFFFFu) : 0xFFFFu;
    return s;
}

void ps2_mdec_write_control(uint32_t v)
{
    TR_LIGAR();
    TR("mdec ctrl %08x", v);
    if (v & (1u << 31)) ps2_mdec_reset();
    controle = v & (3u << 29);
}

void ps2_mdec_dma_in(uint32_t madr, uint32_t palavras)
{
    uint32_t a = madr & 0x1FFFFCu;
    while (palavras--) {
        ps2_mdec_write_data(ps2_ram_get32(a));
        a = (a + 4u) & 0x1FFFFCu;
    }
}

int ps2_mdec_dma_out(uint32_t madr, uint32_t palavras)
{
    uint32_t a = madr & 0x1FFFFCu;
    dec_continuar(palavras);                               /* V48: sob demanda */
    while (palavras && sai_ini < sai_fim) {
        /* V48: trecho contínuo de uma vez, marcando os grânulos de código
         * de 64 bytes uma vez só (antes, palavra a palavra) */
        uint32_t n = sai_fim - sai_ini;
        if (n > palavras) n = palavras;
        if (n > (0x200000u - a) / 4u) n = (0x200000u - a) / 4u;
        memcpy(&g_ps1_ram[a], &saida[sai_ini], n * 4u);
        ps2_cod_marcar_faixa(a, n * 4u);
        sai_ini += n;
        a = (a + n * 4u) & 0x1FFFFCu;
        palavras -= n;
    }
    pend_madr = a;
    pend_palavras = palavras;
    TR("mdec saida madr %08x resta %u fila %u..%u", madr, palavras, sai_ini, sai_fim);
    return palavras == 0;
}
