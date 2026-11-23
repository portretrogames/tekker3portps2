/* mdec_test.c — MDEC: tabelas, macrobloco 15/24 bits e DMA de saída. */
#include "../runtime/mdec.h"
#include "../runtime/ram.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int prontas;
void ps2_dma_mdec_saida_pronta(uint32_t palavras) { (void)palavras; prontas++; }

#define CHECA(c) do { if (!(c)) { printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static void tabelas(void)
{
    uint16_t esc[64];
    int u, x;
    ps2_mdec_write_data(2u << 29 | 1u);            /* luma + croma */
    for (x = 0; x < 32; x++) ps2_mdec_write_data(0x08080808u);  /* qt = 8 */
    for (u = 0; u < 8; u++)
        for (x = 0; x < 8; x++)
            esc[u * 8 + x] = (uint16_t)(int16_t)lround(u == 0 ? 23170.0 :
                32768.0 * cos((2 * x + 1) * u * M_PI / 16.0));
    ps2_mdec_write_data(3u << 29);
    for (x = 0; x < 32; x++)
        ps2_mdec_write_data((uint32_t)esc[2 * x] | (uint32_t)esc[2 * x + 1] << 16);
}

/* Macrobloco com DC de luma = 64 (pixel +64) e croma 0, q_scale=1. */
static void macrobloco(uint32_t prof, uint32_t bit15)
{
    uint16_t h[12];
    int i;
    h[0] = (1u << 10) | 0; h[1] = 0xFE00u;          /* Cr */
    h[2] = (1u << 10) | 0; h[3] = 0xFE00u;          /* Cb */
    for (i = 0; i < 4; i++) { h[4 + 2 * i] = (1u << 10) | 64u; h[5 + 2 * i] = 0xFE00u; }
    ps2_mdec_write_data(1u << 29 | prof << 27 | bit15 << 25 | 6u);
    for (i = 0; i < 6; i++) ps2_mdec_write_data((uint32_t)h[2 * i] | (uint32_t)h[2 * i + 1] << 16);
}

/* ------------------------------------------------------------------
 * V48: decodificador de referência = o de antes da otimização (IDCT com
 * todas as ~1000 multiplicações por bloco e cor pixel a pixel). O novo tem
 * de dar a MESMA saída, palavra por palavra, em fluxos aleatórios.
 * ------------------------------------------------------------------ */
static uint8_t  r_ql[64], r_qc[64];
static int16_t  r_esc[64];
static uint32_t r_saida[0x8000];
static uint32_t r_n;
static const uint8_t r_zz[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};
static int r_s10(uint32_t n) { return (int)((n & 0x3FFu) ^ 0x200u) - 0x200; }
static int r_lim(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static int r_rle(int16_t *blk, const uint8_t *qt, const uint16_t *src, uint32_t *pos, uint32_t total)
{
    uint32_t n; int k, q, val;
    do { if (*pos >= total) return 0; n = src[(*pos)++]; } while (n == 0xFE00u);
    memset(blk, 0, 64 * sizeof(int16_t));
    q = (int)((n >> 10) & 0x3Fu); k = 0; val = r_s10(n) * qt[0];
    for (;;) {
        if (q == 0) val = r_s10(n) * 2;
        val = r_lim(val, -0x400, 0x3FF);
        if (q) blk[r_zz[k]] = (int16_t)val; else blk[k] = (int16_t)val;
        if (*pos >= total) break;
        n = src[(*pos)++];
        k += (int)((n >> 10) & 0x3Fu) + 1;
        if (k > 63) break;
        val = (r_s10(n) * qt[k] * q + 4) >> 3;
    }
    return 1;
}
static void r_idct(int16_t *blk)
{
    int32_t tmp[64]; int x, y, z;
    for (x = 0; x < 8; x++) for (y = 0; y < 8; y++) {
        int32_t s = 0;
        for (z = 0; z < 8; z++) s += blk[y + z * 8] * (r_esc[x + z * 8] >> 3);
        tmp[x + y * 8] = (s + 0xFFF) >> 13;
    }
    for (x = 0; x < 8; x++) for (y = 0; y < 8; y++) {
        int32_t s = 0;
        for (z = 0; z < 8; z++) s += tmp[y + z * 8] * (r_esc[x + z * 8] >> 3);
        blk[x + y * 8] = (int16_t)((s + 0xFFF) >> 13);
    }
}
static uint8_t r_rgb[768];
static void r_yuv(const int16_t *cr, const int16_t *cb, const int16_t *yb, int xx, int yy, int ss)
{
    int x, y;
    for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) {
        int c = ((x + xx) >> 1) + ((y + yy) >> 1) * 8;
        int R = cr[c], B = cb[c], Y = yb[x + y * 8];
        int G = (-352 * B - 731 * R) >> 10;
        uint8_t *o = &r_rgb[((x + xx) + (y + yy) * 16) * 3];
        R = (1436 * R) >> 10; B = (1815 * B) >> 10;
        R = r_lim(Y + R, -128, 127); G = r_lim(Y + G, -128, 127); B = r_lim(Y + B, -128, 127);
        if (ss) { R ^= 0x80; G ^= 0x80; B ^= 0x80; }
        o[0] = (uint8_t)R; o[1] = (uint8_t)G; o[2] = (uint8_t)B;
    }
}
static void r_emitir(const uint8_t *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i += 4u) {
        if (r_n >= sizeof(r_saida) / 4u) return;
        r_saida[r_n++] = (uint32_t)b[i] | (uint32_t)b[i + 1] << 8 | (uint32_t)b[i + 2] << 16 | (uint32_t)b[i + 3] << 24;
    }
}
static void r_decodificar(uint32_t cmd, const uint16_t *src, uint32_t total)
{
    uint32_t prof = (cmd >> 27) & 3u, pos = 0;
    int ss = !((cmd >> 26) & 1u);
    uint16_t bit15 = (cmd >> 25) & 1u ? 0x8000u : 0u;
    static int16_t blk[6][64];
    r_n = 0;
    if (prof <= 1u) {
        while (r_rle(blk[0], r_ql, src, &pos, total)) {
            uint8_t b[64]; int i;
            r_idct(blk[0]);
            for (i = 0; i < 64; i++) { int v = r_lim(blk[0][i], -128, 127); b[i] = (uint8_t)(ss ? v ^ 0x80 : v); }
            if (prof == 1u) r_emitir(b, 64);
            else { uint8_t p[32]; for (i = 0; i < 32; i++) p[i] = (uint8_t)((b[2 * i] >> 4) | (b[2 * i + 1] & 0xF0u)); r_emitir(p, 32); }
        }
        return;
    }
    for (;;) {
        int i;
        if (!r_rle(blk[0], r_qc, src, &pos, total)) break;
        if (!r_rle(blk[1], r_qc, src, &pos, total)) break;
        for (i = 2; i < 6; i++) if (!r_rle(blk[i], r_ql, src, &pos, total)) return;
        for (i = 0; i < 6; i++) r_idct(blk[i]);
        r_yuv(blk[0], blk[1], blk[2], 0, 0, ss); r_yuv(blk[0], blk[1], blk[3], 8, 0, ss);
        r_yuv(blk[0], blk[1], blk[4], 0, 8, ss); r_yuv(blk[0], blk[1], blk[5], 8, 8, ss);
        if (prof == 2u) r_emitir(r_rgb, 768);
        else {
            uint8_t p[512];
            for (i = 0; i < 256; i++) {
                const uint8_t *c = &r_rgb[i * 3];
                uint16_t v = (uint16_t)((c[0] >> 3) | (c[1] >> 3) << 5 | (c[2] >> 3) << 10 | bit15);
                p[2 * i] = (uint8_t)v; p[2 * i + 1] = (uint8_t)(v >> 8);
            }
            r_emitir(p, 512);
        }
    }
}

static uint32_t semente = 12345u;
static uint32_t aleat(void) { semente = semente * 1103515245u + 12345u; return semente >> 8; }

/* Fluxo RLE aleatório: blocos com DC + alguns AC (às vezes muitos, às vezes
 * passando de 63), separados por 0xFE00 (às vezes repetido). */
static uint32_t fluxo(uint16_t *h, uint32_t max, unsigned blocos)
{
    uint32_t n = 0;
    unsigned b;
    for (b = 0; b < blocos && n + 80 < max; b++) {
        unsigned q = aleat() % 8u == 0u ? 0u : 1u + aleat() % 63u, ac, i, k = 0;
        h[n++] = (uint16_t)(q << 10 | (aleat() & 0x3FFu));
        ac = aleat() % 4u == 0u ? 0u : aleat() % 3u == 0u ? 5u + aleat() % 30u : aleat() % 6u;
        for (i = 0; i < ac; i++) {
            unsigned run = aleat() % 5u == 0u ? aleat() % 20u : aleat() % 3u;
            k += run + 1u;
            h[n++] = (uint16_t)(run << 10 | (aleat() & 0x3FFu));
            if (k > 63u) break;
        }
        if (k <= 63u || aleat() % 2u) h[n++] = 0xFE00u;
        if (aleat() % 10u == 0u) h[n++] = 0xFE00u;
    }
    if (n & 1u) h[n++] = 0xFE00u;
    return n;
}

static void comparar_aleatorio(void)
{
    static uint16_t h[4096];
    unsigned rodada, casos = 0, palavras = 0;
    for (rodada = 0; rodada < 3000u; rodada++) {
        uint32_t i, n, cmd;
        /* tabelas: às vezes as reais (cosseno), às vezes aleatórias */
        ps2_mdec_reset();
        ps2_mdec_write_data(2u << 29 | 1u);
        for (i = 0; i < 128u; i++) {
            uint8_t v = (uint8_t)(rodada % 3u == 0u ? 1u + aleat() % 255u : 1u + aleat() % 16u);
            if (i < 64u) r_ql[i] = v; else r_qc[i - 64u] = v;
        }
        for (i = 0; i < 32u; i++) {     /* 16 palavras de luma, 16 de croma */
            const uint8_t *q = i < 16u ? &r_ql[4u * i] : &r_qc[4u * (i - 16u)];
            ps2_mdec_write_data((uint32_t)q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16 | (uint32_t)q[3] << 24);
        }
        for (i = 0; i < 64u; i++) {
            int u = (int)(i / 8u), x = (int)(i % 8u);
            r_esc[i] = rodada % 2u ? (int16_t)lround(u == 0 ? 23170.0 : 32768.0 * cos((2 * x + 1) * u * M_PI / 16.0) * 0.99)
                                   : (int16_t)(aleat() & 0xFFFFu);
        }
        ps2_mdec_write_data(3u << 29);
        for (i = 0; i < 32u; i++)
            ps2_mdec_write_data((uint32_t)(uint16_t)r_esc[2 * i] | (uint32_t)(uint16_t)r_esc[2 * i + 1] << 16);
        n = fluxo(h, sizeof(h) / 2u, 6u + aleat() % 60u);
        cmd = 1u << 29 | (aleat() & 3u) << 27 | (aleat() & 1u) << 26 | (aleat() & 1u) << 25 | n / 2u;
        r_decodificar(cmd, h, n);
        ps2_mdec_write_data(cmd);
        for (i = 0; i < n; i += 2u) ps2_mdec_write_data((uint32_t)h[i] | (uint32_t)h[i + 1] << 16);
        for (i = 0; i < r_n; i++) {
            uint32_t w = ps2_mdec_read_data();
            if (w != r_saida[i]) {
                printf("FALHOU rodada %u: palavra %u de %u: %08x (novo) x %08x (referencia), cmd %08x\n",
                       rodada, (unsigned)i, (unsigned)r_n, (unsigned)w, (unsigned)r_saida[i], (unsigned)cmd);
                exit(1);
            }
        }
        CHECA(ps2_mdec_read_status() & (1u << 31));   /* nem mais nem menos palavras */
        casos++; palavras += r_n;
    }
    printf("MDEC: %u fluxos aleatorios (%u palavras) iguais a referencia\n", casos, palavras);
}

/* V48: a decodificação é sob demanda. Um comando novo (decodificar ou trocar
 * tabela) no meio de uma decodificação tem de terminar a anterior com as
 * tabelas ANTIGAS, e a saída continua na mesma ordem. */
static void tabelas_aleatorias(void)
{
    uint32_t i;
    for (i = 0; i < 64u; i++) { r_ql[i] = (uint8_t)(1u + aleat() % 32u); r_qc[i] = (uint8_t)(1u + aleat() % 32u); }
    ps2_mdec_write_data(2u << 29 | 1u);
    for (i = 0; i < 32u; i++) {
        const uint8_t *q = i < 16u ? &r_ql[4u * i] : &r_qc[4u * (i - 16u)];
        ps2_mdec_write_data((uint32_t)q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16 | (uint32_t)q[3] << 24);
    }
}
static void comando_no_meio(void)
{
    static uint16_t h[2][4096];
    static uint32_t esperado[0x10000];
    unsigned rodada;
    for (rodada = 0; rodada < 300u; rodada++) {
        uint32_t n[2], cmd[2], i, k, total = 0, lidas;
        unsigned j;
        ps2_mdec_reset();
        for (i = 0; i < 64u; i++)
            r_esc[i] = (int16_t)lround(i < 8u ? 23170.0 : 32768.0 * cos((2 * (i % 8u) + 1) * (i / 8u) * M_PI / 16.0) * 0.99);
        ps2_mdec_write_data(3u << 29);
        for (i = 0; i < 32u; i++)
            ps2_mdec_write_data((uint32_t)(uint16_t)r_esc[2 * i] | (uint32_t)(uint16_t)r_esc[2 * i + 1] << 16);
        tabelas_aleatorias();
        for (j = 0; j < 2u; j++) {
            n[j] = fluxo(h[j], 4096u, 12u + aleat() % 40u);
            cmd[j] = 1u << 29 | (aleat() & 3u) << 27 | (aleat() & 1u) << 26 | (aleat() & 1u) << 25 | n[j] / 2u;
        }
        /* 1o comando: referência com as tabelas atuais */
        r_decodificar(cmd[0], h[0], n[0]);
        memcpy(esperado, r_saida, r_n * 4u); total = r_n;
        ps2_mdec_write_data(cmd[0]);
        for (i = 0; i < n[0]; i += 2u) ps2_mdec_write_data((uint32_t)h[0][i] | (uint32_t)h[0][i + 1] << 16);
        /* lê só um pedaço; às vezes troca as tabelas antes do 2o comando */
        lidas = total ? aleat() % total : 0u;
        for (i = 0; i < lidas; i++) CHECA(ps2_mdec_read_data() == esperado[i]);
        if (aleat() % 2u) tabelas_aleatorias();
        r_decodificar(cmd[1], h[1], n[1]);
        memcpy(esperado + total, r_saida, r_n * 4u); total += r_n;
        ps2_mdec_write_data(cmd[1]);
        for (i = 0; i < n[1]; i += 2u) ps2_mdec_write_data((uint32_t)h[1][i] | (uint32_t)h[1][i + 1] << 16);
        for (k = lidas; k < total; k++) {
            uint32_t w = ps2_mdec_read_data();
            if (w != esperado[k]) {
                printf("FALHOU comando no meio: rodada %u palavra %u de %u (lidas antes %u)\n",
                       rodada, (unsigned)k, (unsigned)total, (unsigned)lidas);
                exit(1);
            }
        }
        CHECA(ps2_mdec_read_status() & (1u << 31));
    }
    printf("MDEC: comando novo no meio da decodificacao (300 casos) OK\n");
}

/* V48: grânulos de código marcados pela escrita de DMA (ram.h) */
static int sujo(uint32_t o) { return (g_cod_sujo[o >> 9] >> ((o >> 6) & 7u)) & 1u; }
static void granulos(void)
{
    uint32_t ini0 = g_cod_ini, tam0 = g_cod_tam;
    /* código em [0x1000, 0x1090): o 3º grânulo (0x1080) é parcial */
    memset(g_cod_sujo, 0, PS2_COD_SUJO_BYTES);
    g_cod_ini = 0x1000u; g_cod_tam = 0x90u;
    ps2_cod_marcar_faixa(0x1030u, 33u * 4u);       /* [0x1030, 0x10B4) */
    CHECA(sujo(0x00u) && sujo(0x40u) && sujo(0x80u));
    memset(g_cod_sujo, 0, PS2_COD_SUJO_BYTES);
    ps2_cod_marcar_faixa(0x2000u, 64u);             /* fora do código */
    ps2_cod_marcar_faixa(0x0FC0u, 64u);             /* termina antes */
    CHECA(!sujo(0x00u) && !sujo(0x40u) && !sujo(0x80u));
    ps2_cod_marcar_faixa(0x0FFCu, 8u);              /* atravessa o início */
    CHECA(sujo(0x00u) && !sujo(0x40u));
    /* base desalinhada (0x10004): 4 palavras desde ela = só o 1º grânulo */
    memset(g_cod_sujo, 0, PS2_COD_SUJO_BYTES);
    g_cod_ini = 0x10004u; g_cod_tam = 0x1000u;
    ps2_cod_marcar_faixa(0x10004u, 16u);
    CHECA(sujo(0x00u) && !sujo(0x40u));
    ps2_cod_marcar_faixa(0x10040u, 8u);             /* bytes 0x3C..0x43 */
    CHECA(sujo(0x40u) && !sujo(0x80u));
    memset(g_cod_sujo, 0, PS2_COD_SUJO_BYTES);
    g_cod_ini = ini0; g_cod_tam = tam0;
}

int main(void)
{
    uint32_t w, a;
    granulos();
    comparar_aleatorio();
    comando_no_meio();
    ps2_mdec_reset();
    tabelas();
    CHECA((ps2_mdec_read_status() & 0xFFFFu) == 0xFFFFu);

    /* 15 bits com bit 15: 64 -> 0xC0 sem sinal -> 24 por canal */
    macrobloco(3u, 1u);
    w = ps2_mdec_read_data();
    CHECA(w == 0xE318E318u);   /* 0x8000 | 24 | 24<<5 | 24<<10 = 0xE318 */

    /* 24 bits: 768 bytes = 192 palavras de 0xC0 */
    ps2_mdec_reset();
    macrobloco(2u, 0u);
    /* DMA1 pedido antes de haver dados suficientes: fica pendente */
    ps2_mdec_reset();
    CHECA(ps2_mdec_dma_out(0x80010000u, 192u) == 0);
    macrobloco(2u, 0u);
    CHECA(prontas == 1);
    for (a = 0; a < 192u; a++) CHECA(ps2_ram_get32(0x80010000u + a * 4u) == 0xC0C0C0C0u);
    CHECA(ps2_mdec_read_status() & (1u << 31));     /* saída vazia */

    /* DMA0: comando + parâmetros vindos da RAM */
    {
        uint16_t h[12];
        int i;
        h[0] = (1u << 10); h[1] = 0xFE00u; h[2] = (1u << 10); h[3] = 0xFE00u;
        for (i = 0; i < 4; i++) { h[4 + 2 * i] = (1u << 10) | (uint16_t)(-64 & 0x3FF); h[5 + 2 * i] = 0xFE00u; }
        ps2_ram_set32(0x80020000u, 1u << 29 | 2u << 27 | 6u);
        for (i = 0; i < 6; i++)
            ps2_ram_set32(0x80020004u + (uint32_t)i * 4u, (uint32_t)h[2 * i] | (uint32_t)h[2 * i + 1] << 16);
        ps2_mdec_dma_in(0x80020000u, 7u);
        CHECA(ps2_mdec_dma_out(0x80030000u, 192u) == 1);
        CHECA(ps2_ram_get32(0x80030000u) == 0x40404040u);   /* -64 ^ 0x80 */
    }
    printf("MDEC: tabelas, macroblocos 15/24 bits e DMA OK\n");
    return 0;
}
