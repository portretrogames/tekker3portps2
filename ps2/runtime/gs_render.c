/* gs_render.c — ver gs_render.h. Registradores: GS User's Manual. */
#include "ram.h"
#include "quente.h"
#include "gs_render.h"

#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <dmaKit.h>

int g_gsr_ativo;
unsigned long g_gsr_texflush;
unsigned long long g_prof_espera_gs;
unsigned long long g_prof_gpu_hash;
PS2_QUENTE unsigned long long g_prof_gsr_poli;   /* V28: detalhe do desenho */
unsigned long g_gsr_paginas, g_gsr_envios, g_gsr_envio_px, g_gsr_flushes;
PS2_QUENTE unsigned long g_gsr_prims;

/* ---------------- registradores e formatos do GS ---------------- */
enum {
    R_PRIM = 0x00, R_RGBAQ = 0x01, R_UV = 0x03, R_XYZ2 = 0x05,
    R_TEX0_2 = 0x07, R_CLAMP_2 = 0x09, R_TEX1_2 = 0x15, R_XYOFFSET_2 = 0x19,
    R_PRMODECONT = 0x1A, R_TEXCLUT = 0x1C, R_TEXA = 0x3B, R_TEXFLUSH = 0x3F,
    R_SCISSOR_2 = 0x41, R_ALPHA_2 = 0x43, R_DIMX = 0x44, R_DTHE = 0x45,
    R_COLCLAMP = 0x46, R_TEST_2 = 0x48, R_PABE = 0x49, R_FBA_2 = 0x4B,
    R_FRAME_2 = 0x4D, R_ZBUF_2 = 0x4F, R_BITBLTBUF = 0x50, R_TRXPOS = 0x51,
    R_TRXREG = 0x52, R_TRXDIR = 0x53,
};
enum { PSM_CT16 = 0x02, PSM_T8 = 0x13, PSM_T4 = 0x14 };
#define OFS 2048            /* coordenadas do PS1 podem ser negativas */

/* ---------------- fila de pacotes (2 buffers, DMA para o GIF) ---------------- */
#define FILA_QW 32768u
static uint64_t fila[2][FILA_QW * 2] __attribute__((aligned(64)));
static PS2_QUENTE int      fila_atual;
static PS2_QUENTE uint32_t fila_n;              /* qwords usados no buffer atual */
static PS2_QUENTE int      dma_ocupado;

/*
 * A fila é escrita pelo segmento UCAB (0x30000000): sem cache e com buffer
 * de escrita. Não suja o cache de dados nem precisa de FlushCache antes do
 * DMA (no PS2 real isso custava ~350 KB de faltas de cache por quadro).
 */
#define UCAB(p) ((uint64_t *)(((uintptr_t)(p) & 0x0FFFFFFFu) | 0x30000000u))
static uint64_t *fila_ptr(void) { return UCAB(&fila[fila_atual][fila_n * 2u]); }

void gsr_flush(void)
{
    if (!fila_n) return;
    g_gsr_flushes++;
    if (dma_ocupado) {
        uint32_t t0; __asm__ volatile("mfc0 %0, $9" : "=r"(t0));
        dmaKit_wait(DMA_CHANNEL_GIF, 0);
        { uint32_t t1; __asm__ volatile("mfc0 %0, $9" : "=r"(t1)); g_prof_espera_gs += (uint32_t)(t1 - t0); }
    }
    __asm__ volatile("sync.l");          /* esvazia o buffer de escrita UCAB */
    dmaKit_send_ucab(DMA_CHANNEL_GIF, fila_ptr() - fila_n * 2u, fila_n);
    dma_ocupado = 1;
    fila_atual ^= 1;
    fila_n = 0;
}

static void reservar(uint32_t qw)
{
    if (fila_n + qw > FILA_QW) gsr_flush();
}

/* GIFtag PACKED com n registradores A+D */
static void tag_ad(uint32_t n)
{
    uint64_t *p = fila_ptr();
    p[0] = (uint64_t)n | (1ull << 60);  /* NLOOP=n, NREG=1, FLG=PACKED */
    p[1] = 0xEull;                      /* REGS: A+D */
    fila_n++;
}
static void ad(uint32_t reg, uint64_t valor)
{
    uint64_t *p = fila_ptr();
    p[0] = valor; p[1] = reg;
    fila_n++;
}

/* ---------------- memória do GS ---------------- */
static PS2_QUENTE uint32_t vram_fbp, vram_tbp;     /* buffer da VRAM do PS1 */

typedef struct {
    uint32_t tbp;
    int tx, ty, prof;           /* prof: 0 = 4 bits, 1 = 8 bits; -1 = livre */
    uint64_t versao;            /* versao_pagina: 2 colunas em 64 bits */
    uint32_t uso;
} Fatia;
#define MAX_FATIAS 40
static PS2_QUENTE Fatia fatias[MAX_FATIAS];
static PS2_QUENTE unsigned n_fatias;
static PS2_QUENTE uint32_t relogio_uso;
static PS2_QUENTE uint32_t versao_col[16][2];      /* colunas de 64 px x metades de 256 linhas */

/* ---------------- estado atual (para não repetir registradores) ---------------- */
static PS2_QUENTE uint64_t c_tex0, c_clamp, c_texclut, c_alpha, c_test, c_scissor;
static int c_pabe = -1, c_fba = -1, c_dthe = -1;
static PS2_QUENTE int c_validos;
static PS2_QUENTE uint32_t textura_versao;         /* muda quando a VRAM/cache muda: recarrega CLUT */
/* TEXFLUSH esvazia o pipeline do GS: só quando a memória de textura mudou
 * (uploads, cópias, preenchimentos) ou, para texturas de 15 bits lidas da
 * própria VRAM, depois de algum desenho. Antes ia em toda troca de textura. */
static int textura_suja = 1, desenhou_desde_flush = 1;
static PS2_QUENTE uint32_t c_tex_versao;

void gsr_estado_perdido(void) { c_validos = 0; }
static PS2_QUENTE int chave_valida;

static void base(void)
{
    /* registradores do contexto 2 e globais que o gsKit pode ter mexido */
    reservar(16);
    tag_ad(12);
    ad(R_FRAME_2, (uint64_t)vram_fbp | (16ull << 16) | ((uint64_t)PSM_CT16 << 24));
    ad(R_ZBUF_2, (1ull << 32));                          /* sem escrita de Z */
    ad(R_XYOFFSET_2, (uint64_t)(OFS << 4) | ((uint64_t)(OFS << 4) << 32));
    ad(R_PRMODECONT, 1);
    ad(R_COLCLAMP, 1);
    ad(R_TEX1_2, 0);                                     /* vizinho mais próximo */
    /* TEXA: bit 15 = 0 -> alfa 1; bit 15 = 1 -> 0x80; preto sem bit 15 -> 0 */
    ad(R_TEXA, 0x01ull | (1ull << 15) | (0x80ull << 32));
    /* matriz de dither do PS1 (valores de -4 a 3, 3 bits) */
    {
        static const int m[4][4] = { {-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2} };
        uint64_t d = 0; int i, j;
        for (i = 0; i < 4; i++) for (j = 0; j < 4; j++)
            d |= (uint64_t)(m[i][j] & 7) << (i * 16 + j * 4);
        ad(R_DIMX, d);
    }
    ad(R_DTHE, 0);
    ad(R_PABE, 0);
    ad(R_FBA_2, 0);
    ad(R_TEST_2, 0);
    c_tex0 = c_clamp = c_texclut = c_alpha = c_test = c_scissor = ~0ull;
    c_pabe = 0; c_fba = 0; c_dthe = 0;
    c_validos = 1;
}

int gsr_init(uint32_t vram_bytes, uint32_t cache_bytes, unsigned slots)
{
    unsigned i;
    FlushCache(0);                      /* nada da fila pode estar no cache */
    vram_fbp = vram_bytes / 8192u;
    vram_tbp = vram_bytes / 256u;
    if (slots > MAX_FATIAS) slots = MAX_FATIAS;
    for (i = 0; i < slots; i++) {
        fatias[i].tbp = (cache_bytes + i * 65536u) / 256u;
        fatias[i].prof = -1;
    }
    n_fatias = slots;
    base();
    /* V49: a memória do GS no console vem com o que o OPL/navegador deixou lá
     * (no PCSX2 vem zerada). A sombra da VRAM começa zerada, então a cópia no
     * GS também: antes, área ainda não escrita pelo jogo aparecia como lixo
     * colorido (tela verde antes do "NAMCO PRESENTS" e linha no pé dela). */
    gsr_preencher(0, 0, 1024u, 512u, 0);
    gsr_flush();                        /* o gsr_bench descarta a fila depois */
    dmaKit_wait(DMA_CHANNEL_GIF, 0);
    g_gsr_ativo = 1;
    printf("gs: VRAM do PS1 no GS em %08lx, cache de texturas: %u fatias\n",
           (unsigned long)vram_bytes, slots);
    return 1;
}

/* ---------------- invalidação de texturas ---------------- */
static void invalidar(unsigned x, unsigned y, unsigned w, unsigned h)
{
    unsigned c0 = (x & 1023u) / 64u, c1 = ((x + w - 1u) & 1023u) / 64u, c, l;
    unsigned l0 = (y & 511u) / 256u, l1 = ((y + h - 1u) & 511u) / 256u;
    if (!w || !h) return;
    if (w >= 1024u) { c0 = 0; c1 = 15; }
    if (h >= 512u) { l0 = 0; l1 = 1; }
    for (l = 0; l < 2; l++) {
        if (l0 <= l1 ? (l < l0 || l > l1) : (l > l1 && l < l0)) continue;
        for (c = 0; c < 16; c++) {
            if (c0 <= c1 ? (c < c0 || c > c1) : (c > c1 && c < c0)) continue;
            versao_col[c][l]++;
        }
    }
    textura_versao++;
    textura_suja = 1;
}

static uint64_t versao_pagina(int tx, int ty, int prof)
{
    /* V51: 8 bits ocupa 2 colunas: as duas versões lado a lado em 64 bits
     * (a + b<<16 em 32 bits repetia depois de 65536 envios numa coluna e a
     * textura podia sair velha). Achado na revisão do port do SOTN. */
    unsigned c = (unsigned)tx / 64u, l = (unsigned)ty / 256u;
    uint64_t v = versao_col[c][l];
    if (prof == 1) v |= (uint64_t)versao_col[(c + 1u) & 15u][l] << 32;
    return v;
}

static const uint16_t *sombra_vram;      /* para enviar páginas ao cache */
/* blocos (colunas de 64 px x metades de 256 linhas) já lidos como textura de
 * 4/8 bits: só neles a sombra em RAM precisa refletir preenchimentos */
static PS2_QUENTE uint32_t blocos_textura;
#define BLOCO_TEX(c, l) (1u << (((l) & 1u) * 16u + ((c) & 15u)))

int gsr_area_com_textura(unsigned x, unsigned y, unsigned w, unsigned h)
{
    unsigned c, l, c0, c1, l0, l1;
    if (!w || !h) return 0;
    c0 = (x & 1023u) / 64u; c1 = c0 + (((x & 63u) + w - 1u) / 64u);
    l0 = (y & 511u) / 256u; l1 = l0 + (((y & 255u) + h - 1u) / 256u);
    if (c1 - c0 > 15u) c1 = c0 + 15u;
    if (l1 - l0 > 1u) l1 = l0 + 1u;
    for (l = l0; l <= l1; l++)
        for (c = c0; c <= c1; c++)
            if (blocos_textura & BLOCO_TEX(c, l)) return 1;
    return 0;
}

static void upload_imagem(uint32_t dbp, uint32_t dbw, uint32_t dpsm, unsigned dx, unsigned dy,
                          unsigned w, unsigned h, const void *linhas, unsigned passo_bytes,
                          unsigned bytes_linha)
{
    unsigned y = 0;
    reservar(8);
    tag_ad(4);
    ad(R_BITBLTBUF, ((uint64_t)dbp << 32) | ((uint64_t)dbw << 48) | ((uint64_t)dpsm << 56));
    ad(R_TRXPOS, ((uint64_t)dx << 32) | ((uint64_t)dy << 48));
    ad(R_TRXREG, (uint64_t)w | ((uint64_t)h << 32));
    ad(R_TRXDIR, 0);
    while (y < h) {
        /* quantas linhas cabem neste pedaço (dados em qwords) */
        unsigned total = bytes_linha * (h - y);
        unsigned max = (FILA_QW - 2u) * 16u, linhas_aqui, bytes, qw;
        uint8_t *dst;
        if (total > max) total = (max / bytes_linha) * bytes_linha;
        linhas_aqui = total / bytes_linha;
        bytes = linhas_aqui * bytes_linha;
        qw = (bytes + 15u) / 16u;
        reservar(qw + 1u);
        {
            uint64_t *p = fila_ptr();
            p[0] = (uint64_t)qw | (2ull << 58);      /* FLG = IMAGE */
            p[1] = 0;
            fila_n++;
        }
        dst = (uint8_t *)fila_ptr();
        {
            unsigned k;
            for (k = 0; k < linhas_aqui; k++)
                memcpy(dst + k * bytes_linha,
                       (const uint8_t *)linhas + (size_t)(y + k) * passo_bytes, bytes_linha);
            if (bytes & 15u) memset(dst + bytes, 0, 16u - (bytes & 15u));
        }
        fila_n += qw;
        y += linhas_aqui;
    }
}

static PS2_QUENTE Fatia *ultima_fatia;
static Fatia *pagina_indices(int tx, int ty, int prof)
{
    unsigned i, melhor = 0;
    uint64_t v = versao_pagina(tx, ty, prof);
    Fatia *f = ultima_fatia;
    if (f && f->prof == prof && f->tx == tx && f->ty == ty && f->versao == v) return f;
    for (i = 0; i < n_fatias; i++) {
        f = &fatias[i];
        if (f->prof == prof && f->tx == tx && f->ty == ty) {
            if (f->versao == v) { f->uso = ++relogio_uso; ultima_fatia = f; return f; }
            melhor = i;
            goto enviar;
        }
    }
    for (i = 1; i < n_fatias; i++)
        if (fatias[i].prof < 0 || fatias[i].uso < fatias[melhor].uso) {
            melhor = i;
            if (fatias[i].prof < 0) break;
        }
enviar:
    f = &fatias[melhor];
    f->tx = tx; f->ty = ty; f->prof = prof; f->versao = v; f->uso = ++relogio_uso;
    {   /* 256 linhas a partir de ty; 4 bits = 1 coluna de 64 px, 8 bits = 2 */
        unsigned c, l = (unsigned)ty / 256u, nc = prof ? 2u : 1u, k;
        if ((unsigned)tx & 63u) nc++;           /* (páginas do PS1 são múltiplas de 64) */
        for (k = 0; k < nc; k++) {
            c = (unsigned)tx / 64u + k;
            blocos_textura |= BLOCO_TEX(c, l);
            if ((unsigned)ty & 255u) blocos_textura |= BLOCO_TEX(c, l + 1u);
        }
    }
    {
        /* linhas de 128 (4 bits) ou 256 bytes (8 bits) tiradas da sombra */
        static uint16_t tmp[256 * 128] __attribute__((aligned(16)));
        unsigned largura = prof ? 128u : 64u, y;
        for (y = 0; y < 256u; y++) {
            const uint16_t *lin = &sombra_vram[(((unsigned)ty + y) & 511u) * 1024u];
            if ((unsigned)tx + largura <= 1024u)
                memcpy(&tmp[y * largura], &lin[tx], largura * 2u);
            else {
                unsigned a = 1024u - (unsigned)tx;
                memcpy(&tmp[y * largura], &lin[tx], a * 2u);
                memcpy(&tmp[y * largura + a], lin, (largura - a) * 2u);
            }
        }
        upload_imagem(f->tbp, 4, prof ? PSM_T8 : PSM_T4, 0, 0, 256, 256,
                      tmp, largura * 2u, largura * 2u);
    }
    textura_versao++;
    textura_suja = 1;
    g_gsr_paginas++;
    ultima_fatia = f;
    return f;
}

/* ---------------- estado de desenho ---------------- */
static void set_reg(uint32_t reg, uint64_t v, uint64_t *cache)
{
    if (*cache == v) return;
    *cache = v;
    reservar(2);
    tag_ad(1);
    ad(reg, v);
}

static void estado_comum(const GsrEstado *e, int semi, int textura, int dither)
{
    unsigned x0 = e->area_ini & 1023u, y0 = (e->area_ini >> 10) & 511u;
    unsigned x1 = e->area_fim & 1023u, y1 = (e->area_fim >> 10) & 511u;
    uint64_t test, sc;
    if (!c_validos) base();
    if (x1 > 1023u) x1 = 1023u;
    if (y1 > 511u) y1 = 511u;
    sc = (uint64_t)x0 | ((uint64_t)x1 << 16) | ((uint64_t)y0 << 32) | ((uint64_t)y1 << 48);
    set_reg(R_SCISSOR_2, sc, &c_scissor);
    /* alfa >= 1 passa (descarta texel transparente); máscara: DATE */
    test = 1u | (5u << 1) | (1u << 4) | (1u << 16) | (1u << 17);
    if (e->mascara & 2u) test |= 1u << 14;
    set_reg(R_TEST_2, test, &c_test);
    if (semi) {
        static const uint64_t modos[4] = {
            0u | (1u << 2) | (2u << 4) | (1u << 6) | (0x40ull << 32),
            0u | (2u << 2) | (2u << 4) | (1u << 6) | (0x80ull << 32),
            1u | (0u << 2) | (2u << 4) | (2u << 6) | (0x80ull << 32),
            0u | (2u << 2) | (2u << 4) | (1u << 6) | (0x20ull << 32),
        };
        set_reg(R_ALPHA_2, modos[(e->modo >> 5) & 3u], &c_alpha);
    }
    {
        int pabe = semi && textura;     /* textura: só texels com bit 15 */
        int fba = (int)(e->mascara & 1u);
        if (pabe != c_pabe || fba != c_fba || dither != c_dthe) {
            reservar(4);
            tag_ad(3);
            ad(R_PABE, (uint64_t)pabe);
            ad(R_FBA_2, (uint64_t)fba);
            ad(R_DTHE, (uint64_t)dither);
            c_pabe = pabe; c_fba = fba; c_dthe = dither;
        }
    }
}

/* Configura a textura; devolve o deslocamento de UV (página de 15 bits). */
static void estado_textura(const GsrEstado *e, unsigned op, uint32_t clut, int *du, int *dv)
{
    int tx = (int)(e->modo & 15u) * 64, ty = (int)((e->modo >> 4) & 1u) * 256;
    unsigned prof = (e->modo >> 7) & 3u;
    uint64_t tfx = (op & 1u) ? 1u : 0u;              /* bruto: DECAL; senão MODULATE */
    uint64_t tex0, clamp;
    if (prof >= 2u) {
        tex0 = (uint64_t)vram_tbp | (16ull << 14) | ((uint64_t)PSM_CT16 << 20) |
               (10ull << 26) | (9ull << 30) | (1ull << 34) | (tfx << 35);
        clamp = 2u | (2u << 2) | ((uint64_t)tx << 4) | ((uint64_t)(tx + 255 > 1023 ? 1023 : tx + 255) << 14) |
                ((uint64_t)ty << 24) | ((uint64_t)(ty + 255) << 34);
        *du = tx; *dv = ty;
    } else {
        Fatia *f = pagina_indices(tx, ty, (int)prof);
        unsigned um = e->janela & 31u, vm = (e->janela >> 5) & 31u;
        unsigned uf = (e->janela >> 10) & 31u, vf = (e->janela >> 15) & 31u;
        uint64_t texclut = 16u | ((uint64_t)(clut & 63u) << 6) | ((uint64_t)((clut >> 6) & 511u) << 12);
        if (texclut != c_texclut) c_tex0 = ~0ull;     /* nova CLUT: recarregar */
        set_reg(R_TEXCLUT, texclut, &c_texclut);
        tex0 = (uint64_t)f->tbp | (4ull << 14) | ((uint64_t)(prof ? PSM_T8 : PSM_T4) << 20) |
               (8ull << 26) | (8ull << 30) | (1ull << 34) | (tfx << 35) |
               ((uint64_t)vram_tbp << 37) | ((uint64_t)PSM_CT16 << 51) | (1ull << 55) |
               (1ull << 61);                               /* CSM2, carrega a CLUT */
        clamp = 3u | (3u << 2) |
                ((uint64_t)((~(um << 3)) & 0xFFu) << 4) | ((uint64_t)((uf & um) << 3) << 14) |
                ((uint64_t)((~(vm << 3)) & 0xFFu) << 24) | ((uint64_t)((vf & vm) << 3) << 34);
        *du = 0; *dv = 0;
    }
    set_reg(R_CLAMP_2, clamp, &c_clamp);
    if (textura_suja || (prof >= 2u && desenhou_desde_flush)) {
        reservar(2); tag_ad(1); ad(R_TEXFLUSH, 0);
        textura_suja = 0; desenhou_desde_flush = 0;
        g_gsr_texflush++;
    }
    if (tex0 != c_tex0 || c_tex_versao != textura_versao) {
        c_tex0 = tex0;
        c_tex_versao = textura_versao;
        reservar(2);
        tag_ad(1);
        ad(R_TEX0_2, tex0);
    }
}

/* V51: X/Y do GS são 12.4 sem sinal em 16 bits. Com OFS=2048 o PS1 cabe
 * (-2048..2046), mas sprite + largura passa de 4095 e vazaria para o Y:
 * prende em 0..4095 (achado na revisão do port do SOTN). */
static inline uint32_t coord(int c)
{
    c += OFS;
    if (__builtin_expect((unsigned)c > 4095u, 0)) c = c < 0 ? 0 : 4095;
    return (uint32_t)c << 4;
}
static uint64_t xyz(int x, int y) { return (uint64_t)coord(x) | ((uint64_t)coord(y) << 16); }
/* Vértice de polígono: 11 bits com sinal + deslocamento de 11 bits com sinal
 * (gpu.c vertice() / gsr_poligono_pk) = -2048..2046, + OFS = 0..4094: sempre
 * cabe, sem o limite (que custava ~57 ciclos por polígono no bench). */
static inline uint64_t xyz_vert(int x, int y)
{
    return (uint64_t)((uint32_t)(x + OFS) << 4) | ((uint64_t)((uint32_t)(y + OFS) << 4) << 16);
}
static uint64_t uv(int u, int v)  { return (uint64_t)((u << 4) & 0x3FFF) | ((uint64_t)((v << 4) & 0x3FFF) << 16); }
#define MEIO_TEXEL (8ull | (8ull << 16))   /* +0,5 texel em U e V (somado a um uv() inteiro) */
static uint64_t rgba(int r, int g, int b, int a)
{
    return (uint64_t)((uint32_t)(r & 0xFFu) | (uint32_t)(g & 0xFFu) << 8 | (uint32_t)(b & 0xFFu) << 16 | (uint32_t)(a & 0xFFu) << 24) | (0x3F800000ull << 32);
}

/* ---------------- chave de estado ---------------- */
/* Se nada que afeta os registradores mudou desde o último desenho, pula
 * estado_comum/estado_textura (a maior parte dos polígonos de uma lista). */
typedef struct { uint32_t modo, janela, area_ini, area_fim, mascara, op, clut, versao; } Chave;
static PS2_QUENTE Chave ultima_chave;
static PS2_QUENTE int du_ult, dv_ult;

static inline int mesma_chave(const GsrEstado *e, unsigned op_bits, uint32_t clut)
{
    Chave k;
    /* V25: caminho comum (nada mudou) sem montar a chave nem memcmp */
    if (chave_valida && c_validos &&
        e->modo == ultima_chave.modo && op_bits == ultima_chave.op &&
        clut == ultima_chave.clut && textura_versao == ultima_chave.versao &&
        e->janela == ultima_chave.janela && e->area_ini == ultima_chave.area_ini &&
        e->area_fim == ultima_chave.area_fim && e->mascara == ultima_chave.mascara)
        return 1;
    k.modo = e->modo; k.janela = e->janela; k.area_ini = e->area_ini; k.area_fim = e->area_fim;
    k.mascara = e->mascara; k.op = op_bits; k.clut = clut; k.versao = textura_versao;
    if (chave_valida && c_validos && memcmp(&k, &ultima_chave, sizeof(k)) == 0) return 1;
    ultima_chave = k;
    chave_valida = 1;
    return 0;
}

/* ---------------- primitivas ---------------- */
void gsr_poligono(const GsrEstado *e, const GsrVertice *v, int n, unsigned op, uint32_t clut)
{
    int tex = (op & 4u) != 0, semi = (op & 2u) != 0, gouraud = (op & 16u) != 0;
    int du = 0, dv = 0, i, a = tex ? 0x80 : 0x7F;
    uint64_t prim = 4u /* tristrip */ | (1u << 9) /* CTXT 2 */ | (1u << 8) /* UV */;
    uint32_t tp0, tp1;
#ifndef PS2_LIMPO
    __asm__ volatile("mfc0 %0, $9" : "=r"(tp0));
#else
    tp0 = 0;
#endif
    g_gsr_prims++;
    desenhou_desde_flush = 1;
    /* op relevante para o estado: semi, textura, bruto, gouraud (dither) */
    /* textura de 15 bits ignora a CLUT (lixo no pacote não troca o estado) */
    if (!mesma_chave(e, (op & 0x17u) | 0x100u, (tex && ((e->modo >> 7) & 3u) < 2u) ? clut : 0u)) {
        estado_comum(e, semi, tex, gouraud && (e->modo & 0x200u));
        if (tex) estado_textura(e, op, clut, &du, &dv);
        /* estado_textura pode subir textura_versao (página nova): atualiza a chave */
        ultima_chave.versao = textura_versao;
        du_ult = du; dv_ult = dv;
    } else { du = du_ult; dv = dv_ult; }
    if (gouraud) prim |= 1u << 3;
    if (tex) prim |= 1u << 4;
    if (semi) prim |= 1u << 6;
    /* (V27 testou REGLIST aqui: no console cada polígono ficou ~700 ciclos
     * mais caro. Voltou o PACKED A+D.) */
    reservar(2u + 3u * (unsigned)n);
    {
        uint64_t *p = fila_ptr();
        unsigned k = 1u + (tex ? 3u : 2u) * (unsigned)n;
        uint64_t cor0 = rgba(v[0].r, v[0].g, v[0].b, a);
        if (tex && (op & 1u)) cor0 = rgba(0x80, 0x80, 0x80, a);
        p[0] = (uint64_t)k | (1ull << 60); p[1] = 0xEull;
        p[2] = prim; p[3] = R_PRIM;
        p += 4;
        for (i = 0; i < n; i++) {
            p[0] = (gouraud && !(tex && (op & 1u))) ? rgba(v[i].r, v[i].g, v[i].b, a) : cor0;
            p[1] = R_RGBAQ;
            p += 2;
            if (tex) {
                p[0] = uv(v[i].u + du, v[i].v + dv); p[1] = R_UV;
                p += 2;
            }
            p[0] = xyz_vert(v[i].x, v[i].y); p[1] = R_XYZ2;
            p += 2;
        }
        fila_n += 1u + k;
    }
#ifndef PS2_LIMPO
    __asm__ volatile("mfc0 %0, $9" : "=r"(tp1));
    g_prof_gsr_poli += (uint32_t)(tp1 - tp0);
#else
    (void)tp0; (void)tp1;
#endif
}

static inline int s11(uint32_t n) { return (int)((int32_t)(n << 21) >> 21); }

/* V29: mesmo resultado de gpu.c poligono() + gsr_poligono(), lendo o pacote
 * GP0 direto (cor 0x00BBGGRR já está no formato do RGBAQ). */
void gsr_poligono_pk(const GsrEstado *e, const uint32_t *pk, unsigned op, uint32_t clut, int ox, int oy)
{
    int tex = (op & 4u) != 0, semi = (op & 2u) != 0, gouraud = (op & 16u) != 0;
    unsigned n = (op & 8u) ? 4u : 3u, i;
    uint32_t cor[4], uvw[4] = { 0, 0, 0, 0 };
    int xs[4], ys[4], minx, maxx, miny, maxy;
    int du = 0, dv = 0;
    uint32_t a = tex ? 0x80u : 0x7Fu;
    uint64_t prim = 4u | (1u << 9) | (1u << 8);
    const uint32_t *q = pk + 1;
    uint32_t tp0, tp1;
#ifndef PS2_LIMPO
    __asm__ volatile("mfc0 %0, $9" : "=r"(tp0));
#else
    tp0 = 0;
#endif
    cor[0] = pk[0];
    for (i = 0; i < n; i++) {
        uint32_t pos;
        if (i) cor[i] = gouraud ? *q++ : cor[0];
        pos = *q++;
        if (tex) uvw[i] = *q++;
        xs[i] = s11(pos) + ox;
        ys[i] = s11(pos >> 16) + oy;
    }
    minx = maxx = xs[0]; miny = maxy = ys[0];
    for (i = 1; i < n; i++) {
        if (xs[i] < minx) minx = xs[i];
        if (xs[i] > maxx) maxx = xs[i];
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
    }
    if (maxx - minx > 1023 || maxy - miny > 511) return;
    g_gsr_prims++;
    desenhou_desde_flush = 1;
    if (!mesma_chave(e, (op & 0x17u) | 0x100u, (tex && ((e->modo >> 7) & 3u) < 2u) ? clut : 0u)) {
        estado_comum(e, semi, tex, gouraud && (e->modo & 0x200u));
        if (tex) estado_textura(e, op, clut, &du, &dv);
        ultima_chave.versao = textura_versao;
        du_ult = du; dv_ult = dv;
    } else { du = du_ult; dv = dv_ult; }
    if (gouraud) prim |= 1u << 3;
    if (tex) prim |= 1u << 4;
    if (semi) prim |= 1u << 6;
    reservar(2u + 3u * n);
    {
        uint64_t *p = fila_ptr();
        unsigned k = 1u + (tex ? 3u : 2u) * n;
        int bruto = tex && (op & 1u);
        uint64_t q32 = 0x3F800000ull << 32, aa = (uint64_t)(a << 24);
        uint64_t cor0 = bruto ? (0x808080u | aa | q32) : ((cor[0] & 0xFFFFFFu) | aa | q32);
        p[0] = (uint64_t)k | (1ull << 60); p[1] = 0xEull;
        p[2] = prim; p[3] = R_PRIM;
        p += 4;
        for (i = 0; i < n; i++) {
            p[0] = (gouraud && !bruto) ? ((cor[i] & 0xFFFFFFu) | aa | q32) : cor0;
            p[1] = R_RGBAQ;
            p += 2;
            if (tex) {
                uint32_t u = ((uvw[i] & 255u) + (uint32_t)du) << 4, v = (((uvw[i] >> 8) & 255u) + (uint32_t)dv) << 4;
                p[0] = (uint64_t)(u & 0x3FFFu) | ((uint64_t)(v & 0x3FFFu) << 16); p[1] = R_UV;
                p += 2;
            }
            p[0] = xyz_vert(xs[i], ys[i]);
            p[1] = R_XYZ2;
            p += 2;
        }
        fila_n += 1u + k;
    }
#ifndef PS2_LIMPO
    __asm__ volatile("mfc0 %0, $9" : "=r"(tp1));
    g_prof_gsr_poli += (uint32_t)(tp1 - tp0);
#else
    (void)tp0; (void)tp1;
#endif
}

void gsr_sprite(const GsrEstado *e, const GsrVertice *p, int w, int h, unsigned op, uint32_t clut)
{
    int tex = (op & 4u) != 0, semi = (op & 2u) != 0;
    int du = 0, dv = 0, a = tex ? 0x80 : 0x7F, r = p->r, g = p->g, b = p->b;
    uint64_t prim = 6u /* sprite */ | (1u << 9) | (1u << 8);
    if (!w || !h) return;
    desenhou_desde_flush = 1;
    if (!mesma_chave(e, (op & 0x07u) | 0x200u, (tex && ((e->modo >> 7) & 3u) < 2u) ? clut : 0u)) {
        estado_comum(e, semi, tex, 0);
        if (tex) estado_textura(e, op, clut, &du, &dv);
        ultima_chave.versao = textura_versao;
        du_ult = du; dv_ult = dv;
    } else { du = du_ult; dv = dv_ult; }
    if (tex) prim |= 1u << 4;
    if (semi) prim |= 1u << 6;
    if (tex && (op & 1u)) r = g = b = 0x80;
    reservar(9);
    tag_ad(tex ? 6u : 4u);
    ad(R_PRIM, prim);
    ad(R_RGBAQ, rgba(r, g, b, a));
    /* V50: UV no centro do texel (+8 = meio texel). Testes no GS de verdade
     * (PCSX2 PR 14029): num sprite de largura/altura que não é potência de 2,
     * o U/V interpolado em cima de um inteiro arredonda PARA BAIXO, menos na
     * 1ª coluna/linha. Com UV inteiro, cada letra do texto do PUSH START saía
     * deslocada 1 texel (1ª coluna e 1ª linha repetidas, parte de baixo
     * "puxada"); o PCSX2 arredonda para cima e mostrava certo. O sprite do
     * PS1 é sempre 1:1, então o meio texel escolhe o mesmo texel nos dois. */
    if (tex) ad(R_UV, uv(p->u + du, p->v + dv) + MEIO_TEXEL);
    ad(R_XYZ2, xyz(p->x, p->y));
    if (tex) ad(R_UV, uv(p->u + du + w, p->v + dv + h) + MEIO_TEXEL);
    ad(R_XYZ2, xyz(p->x + w, p->y + h));
}

void gsr_linha(const GsrEstado *e, const GsrVertice *p, const GsrVertice *q, unsigned op)
{
    int semi = (op & 2u) != 0, gouraud = (op & 16u) != 0;
    uint64_t prim = 1u /* linha */ | (1u << 9) | (1u << 8);
    chave_valida = 0;
    desenhou_desde_flush = 1;       /* V51: textura de 15 bits lida depois precisa de TEXFLUSH */
    estado_comum(e, semi, 0, 0);
    if (gouraud) prim |= 1u << 3;
    if (semi) prim |= 1u << 6;
    reservar(7);
    tag_ad(5);
    ad(R_PRIM, prim);
    ad(R_RGBAQ, rgba(p->r, p->g, p->b, 0x7F));
    ad(R_XYZ2, (uint64_t)(coord(p->x) + 8u) | ((uint64_t)(coord(p->y) + 8u) << 16));
    ad(R_RGBAQ, gouraud ? rgba(q->r, q->g, q->b, 0x7F) : rgba(p->r, p->g, p->b, 0x7F));
    ad(R_XYZ2, (uint64_t)(coord(q->x) + 8u) | ((uint64_t)(coord(q->y) + 8u) << 16));
}

void gsr_preencher(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t cor)
{
    unsigned r = (cor & 31u) << 3, g = ((cor >> 5) & 31u) << 3, b = ((cor >> 10) & 31u) << 3;
    unsigned x1 = x + w - 1u, y1 = y + h - 1u;
    if (!w || !h) return;
    if (!c_validos) base();
    if (x1 > 1023u) x1 = 1023u;
    if (y1 > 511u) y1 = 511u;
    reservar(16);
    tag_ad(11);
    ad(R_SCISSOR_2, (uint64_t)x | ((uint64_t)x1 << 16) | ((uint64_t)y << 32) | ((uint64_t)y1 << 48));
    ad(R_TEST_2, (1u << 16) | (1u << 17));          /* sem teste de alfa nem máscara */
    ad(R_FBA_2, 0);
    ad(R_DTHE, 0);
    ad(R_PRIM, 6u | (1u << 9));
    ad(R_RGBAQ, rgba((int)r, (int)g, (int)b, 0));
    ad(R_XYZ2, xyz((int)x, (int)y));
    ad(R_XYZ2, xyz((int)x1 + 1, (int)y1 + 1));
    ad(R_SCISSOR_2, c_scissor != ~0ull ? c_scissor : 0);
    ad(R_TEST_2, c_test != ~0ull ? c_test : 0);
    ad(R_FBA_2, (uint64_t)(c_fba > 0));
    if (c_dthe > 0) { reservar(2); tag_ad(1); ad(R_DTHE, 1); }
    invalidar(x, y, w, h);
    chave_valida = 0;
}

static void copiar_pedaco(unsigned sx, unsigned sy, unsigned dx, unsigned dy, unsigned w, unsigned h);

/* V51: a VRAM do PS1 dá a volta em 1024x512 (a sombra em gpu.c também); no
 * GS um retângulo que passa da borda iria para fora da área. Divide nas
 * bordas da origem e do destino, em ordem compatível com a direção. */
void gsr_copiar(unsigned sx, unsigned sy, unsigned dx, unsigned dy, unsigned w, unsigned h)
{
    unsigned cx[4], cy[4], nx = 0, ny = 0, a, b, k, t;
    if (!w || !h) return;
    sx &= 1023u; dx &= 1023u; sy &= 511u; dy &= 511u;
    cx[nx++] = 0;
    if (sx + w > 1024u) cx[nx++] = 1024u - sx;
    if (dx + w > 1024u && 1024u - dx != (nx > 1 ? cx[1] : 0u)) cx[nx++] = 1024u - dx;
    if (nx == 3 && cx[2] < cx[1]) { t = cx[1]; cx[1] = cx[2]; cx[2] = t; }
    cx[nx] = w;
    cy[ny++] = 0;
    if (sy + h > 512u) cy[ny++] = 512u - sy;
    if (dy + h > 512u && 512u - dy != (ny > 1 ? cy[1] : 0u)) cy[ny++] = 512u - dy;
    if (ny == 3 && cy[2] < cy[1]) { t = cy[1]; cy[1] = cy[2]; cy[2] = t; }
    cy[ny] = h;
    for (b = 0; b < ny; b++) {
        unsigned yb = dy > sy ? ny - 1u - b : b;
        for (k = 0; k < nx; k++) {
            unsigned xa = dx > sx ? nx - 1u - k : k;
            a = cx[xa];
            copiar_pedaco((sx + a) & 1023u, (sy + cy[yb]) & 511u, (dx + a) & 1023u, (dy + cy[yb]) & 511u,
                          cx[xa + 1u] - a, cy[yb + 1u] - cy[yb]);
        }
    }
}

static void copiar_pedaco(unsigned sx, unsigned sy, unsigned dx, unsigned dy, unsigned w, unsigned h)
{
    unsigned dir = 0;
    if (!w || !h) return;
    if (!c_validos) base();
    /* V51: TRXPOS.DIR (GS User's Manual): bit 0 = começa embaixo (de baixo
     * para cima), bit 1 = começa à direita. Estava trocado: cópia com
     * sobreposição saía errada (achado na revisão do port do SOTN). */
    if (dy > sy) dir |= 1u;          /* de baixo para cima */
    if (dx > sx) dir |= 2u;          /* da direita para a esquerda */
    reservar(6);
    tag_ad(4);
    ad(R_BITBLTBUF, (uint64_t)vram_tbp | (16ull << 16) | ((uint64_t)PSM_CT16 << 24) |
       ((uint64_t)vram_tbp << 32) | (16ull << 48) | ((uint64_t)PSM_CT16 << 56));
    ad(R_TRXPOS, (uint64_t)sx | ((uint64_t)sy << 16) | ((uint64_t)dx << 32) |
       ((uint64_t)dy << 48) | ((uint64_t)dir << 59));
    ad(R_TRXREG, (uint64_t)w | ((uint64_t)h << 32));
    ad(R_TRXDIR, 2);
    invalidar(dx, dy, w, h);
}

void gsr_enviar(const uint16_t *sombra, unsigned x, unsigned y, unsigned w, unsigned h)
{
    sombra_vram = sombra;
    g_gsr_envios++; g_gsr_envio_px += w * h;
    if (!w || !h) return;
    if (!c_validos) base();
    y &= 511u; x &= 1023u;
    if (y + h > 512u) {                           /* cruza a última linha: a VRAM dá a volta */
        unsigned a = 512u - y;
        gsr_enviar(sombra, x, y, w, a);
        gsr_enviar(sombra, x, 0, w, h - a);
        return;
    }
    if (x + w > 1024u) {
        unsigned a = 1024u - x;
        gsr_enviar(sombra, x, y, a, h);
        gsr_enviar(sombra, 0, y, w - a, h);
        return;
    }
    {
        /* linhas de largura w; a fila exige múltiplos de 16 bytes por pedaço,
         * então para larguras ímpares de 8 pixels envia linha a linha */
        unsigned bytes_linha = w * 2u;
        if (bytes_linha & 15u) {
            unsigned k;
            for (k = 0; k < h; k++)
                upload_imagem(vram_tbp, 16, PSM_CT16, x, y + k, w, 1,
                              &sombra[(y + k) * 1024u + x], 2048u, bytes_linha);
        } else
            upload_imagem(vram_tbp, 16, PSM_CT16, x, y, w, h,
                          &sombra[y * 1024u + x], 2048u, bytes_linha);
    }
    invalidar(x, y, w, h);
}

/* A sombra também é usada ao montar páginas do cache. */
void gsr_definir_sombra(const uint16_t *sombra) { sombra_vram = sombra; }

/* ---------------- painel de desempenho (texto na tela) ---------------- */
static char painel[160];
static int  painel_ligado = 1;
void gsr_painel(const char *texto) { strncpy(painel, texto, sizeof(painel) - 1u); }
void gsr_painel_alternar(void) { painel_ligado = !painel_ligado; }

/* fonte 3x5: 5 linhas de 3 bits (bit 2 = coluna da esquerda) */
static uint16_t glifo(char c)
{
    static const struct { char c; uint16_t g; } f[] = {
        {'0',075557},{'1',026227},{'2',071747},{'3',071717},{'4',055711},{'5',074717},
        {'6',074757},{'7',071111},{'8',075757},{'9',075717},{'%',051245},{':',002020},
        {'/',011244},{'.',000002},{'-',000700},{'A',025755},{'B',065656},{'C',074447},
        {'D',065556},{'E',074747},{'F',074744},{'G',074557},{'H',055755},{'I',072227},
        {'J',011157},{'K',055655},{'L',044447},{'M',057755},{'N',065555},{'O',075557},
        {'P',075744},{'Q',075571},{'R',075655},{'S',074717},{'T',072222},{'U',055557},
        {'V',055552},{'W',055775},{'X',055255},{'Y',055222},{'Z',071247},
    };
    unsigned i;
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    for (i = 0; i < sizeof(f) / sizeof(f[0]); i++) if (f[i].c == c) return f[i].g;
    return 0;
}

static void desenhar_painel(unsigned x0, unsigned y0)
{
    const unsigned esc = 2;             /* cada ponto da fonte = 2x2 pixels */
    unsigned col = 0, lin = 0, largura = 0, linhas = 1, i;
    const char *t;
    if (!painel_ligado || !painel[0]) return;
    for (t = painel, i = 0; *t; t++) {
        if (*t == '\n') { linhas++; i = 0; continue; }
        if (++i > largura) largura = i;
    }
    /* fundo preto */
    reservar(5); tag_ad(3);
    ad(R_PRIM, 6u | (1u << 9));
    ad(R_RGBAQ, rgba(0, 0, 0, 0x80));
    ad(R_XYZ2, (uint64_t)((x0 - 2u) << 4) | ((uint64_t)((y0 - 2u) << 4) << 16));
    reservar(2); tag_ad(1);
    ad(R_XYZ2, (uint64_t)((x0 + largura * 4u * esc + 2u) << 4) |
               ((uint64_t)((y0 + linhas * 6u * esc + 2u) << 4) << 16));
    reservar(2); tag_ad(1);
    ad(R_RGBAQ, rgba(0xFF, 0xFF, 0x40, 0x80));
    /* pontos juntados em lotes: um GIFtag para até 128 pontos (antes era um
     * por ponto, ~600 por VBlank — auditoria V19 2.2) */
    {
    static uint64_t lote[256];
    unsigned nl = 0, k;
    for (t = painel; *t; t++) {
        uint16_t g;
        unsigned r, c;
        if (*t == '\n') { lin++; col = 0; continue; }
        g = glifo(*t);
        for (r = 0; r < 5u; r++)
            for (c = 0; c < 3u; c++)
                if (g & (1u << ((4u - r) * 3u + (2u - c)))) {
                    unsigned px = x0 + (col * 4u + c) * esc, py = y0 + (lin * 6u + r) * esc;
                    lote[nl++] = (uint64_t)(px << 4) | ((uint64_t)(py << 4) << 16);
                    lote[nl++] = (uint64_t)((px + esc) << 4) | ((uint64_t)((py + esc) << 4) << 16);
                    if (nl == 256u) {
                        reservar(1u + nl); tag_ad(nl);
                        for (k = 0; k < nl; k++) ad(R_XYZ2, lote[k]);
                        nl = 0;
                    }
                }
        col++;
    }
    if (nl) {
        reservar(1u + nl); tag_ad(nl);
        for (k = 0; k < nl; k++) ad(R_XYZ2, lote[k]);
    }
    }
}

/* Copia a área exibida da VRAM do PS1 para a tela (buffer do gsKit). */
void gsr_exibir(uint32_t tela_bytes, unsigned tela_fbw, unsigned tela_psm, unsigned tela_w, unsigned tela_h,
                unsigned x, unsigned y, unsigned w, unsigned h)
{
    /* V49: amostra no centro de cada pixel da tela (meio texel de folga) e
     * prende a leitura na área exibida (REGION_CLAMP): no GS de verdade a
     * amostra em cima da borda de um texel pode cair no vizinho, e a última
     * linha/coluna lia a VRAM fora da tela */
    uint64_t clamp = 0;                 /* REPEAT: a área que dá a volta na VRAM continua do outro lado */
    uint32_t meio_u, meio_v, u_fim;
    if (!w || !h || !tela_w || !tela_h) return;
    x &= 1023u; y &= 511u;
    meio_u = (w * 8u) / tela_w; meio_v = (h * 8u) / tela_h;
    if (x + w <= 1024u) clamp |= 2u | ((uint64_t)x << 4) | ((uint64_t)(x + w - 1u) << 14);
    if (y + h <= 512u) clamp |= (2u << 2) | ((uint64_t)y << 24) | ((uint64_t)(y + h - 1u) << 34);
    /* U tem 14 bits (10.4): x + w = 1024 passaria de 16383 */
    u_fim = ((x + w) << 4) + meio_u;
    if (u_fim > 16383u) u_fim = 16383u;
    reservar(16);
    tag_ad(11);
    ad(R_PRMODECONT, 1);            /* V51: PRIM manda nos atributos (o gsKit pode ter mudado) */
    ad(R_CLAMP_2, clamp);
    ad(R_TEX1_2, 0);
    ad(R_FRAME_2, (uint64_t)(tela_bytes / 8192u) | ((uint64_t)tela_fbw << 16) | ((uint64_t)tela_psm << 24));
    ad(R_XYOFFSET_2, 0);
    ad(R_SCISSOR_2, (uint64_t)(tela_w - 1u) << 16 | (uint64_t)(tela_h - 1u) << 48);
    ad(R_TEST_2, (1u << 16) | (1u << 17));
    ad(R_DTHE, 0);
    ad(R_FBA_2, 0);
    ad(R_TEXFLUSH, 0);
    ad(R_TEX0_2, (uint64_t)vram_tbp | (16ull << 14) | ((uint64_t)PSM_CT16 << 20) |
                 (10ull << 26) | (9ull << 30) | (1ull << 35));      /* DECAL, RGB */
    /* em faixas de 32 linhas da tela (sprites grandes texturizados no GS
     * são mais lentos e, no teste, a amostragem travava depois de ~100 linhas) */
    {
        unsigned faixa, n = (tela_h + 31u) / 32u;
        reservar(2u + n * 5u);
        tag_ad(1u + n * 4u);
        ad(R_PRIM, 6u | (1u << 4) | (1u << 8) | (1u << 9));
        for (faixa = 0; faixa < n; faixa++) {
            unsigned y0 = faixa * 32u, y1 = y0 + 32u > tela_h ? tela_h : y0 + 32u;
            /* V em 1/16 de texel: y + h * linha / tela_h (+ meio pixel) */
            uint32_t v0 = (y << 4) + (uint32_t)(((uint64_t)(h << 4) * y0) / tela_h) + meio_v;
            uint32_t v1 = (y << 4) + (uint32_t)(((uint64_t)(h << 4) * y1) / tela_h) + meio_v;
            ad(R_UV, (uint64_t)((x << 4) + meio_u) | ((uint64_t)v0 << 16));
            ad(R_XYZ2, (uint64_t)(y0 << 4) << 16);
            ad(R_UV, (uint64_t)u_fim | ((uint64_t)v1 << 16));
            ad(R_XYZ2, (uint64_t)(tela_w << 4) | ((uint64_t)(y1 << 4) << 16));
        }
    }
    desenhar_painel(12, 12);
    c_validos = 0;              /* base() restaura FRAME_2 etc. */
    gsr_flush();
}

/* V29: medição no console (uma vez, no boot). O PCSX2 não reproduz o custo
 * de memória do EE; estes números dizem onde o desenho gasta ciclos. */
static inline uint32_t bench_t(void) { uint32_t t; __asm__ volatile("mfc0 %0, $9" : "=r"(t)); return t; }
void gsr_bench(void)
{
    uint32_t t0, t1, t2, t3, t4, soma = 0;
    unsigned i;
    uint64_t *u = UCAB(&fila[1][0]), *c = &fila[1][0];
    volatile uint64_t *s = (volatile uint64_t *)0x70000000u;
    t0 = bench_t();
    for (i = 0; i < 8192u; i += 2u) { u[i] = i; u[i + 1] = 5u; }
    __asm__ volatile("sync.l");
    t1 = bench_t();
    for (i = 0; i < 8192u; i += 2u) { c[i] = i; c[i + 1] = 5u; }
    t2 = bench_t();
    FlushCache(0);
    t3 = bench_t();
    for (i = 0; i < 2048u; i += 2u) { s[i] = i; s[i + 1] = 5u; }
    t4 = bench_t();
    printf("bench: escrita por qword: UCAB %lu, cache %lu (+ writeback %lu), scratchpad %lu ciclos/100\n",
           (unsigned long)((t1 - t0) * 100u / 4096u), (unsigned long)((t2 - t1) * 100u / 4096u),
           (unsigned long)((t3 - t2) * 100u / 4096u), (unsigned long)((t4 - t3) * 100u / 1024u));
    FlushCache(0);
    t0 = bench_t();
    for (i = 0; i < 16384u; i++) soma += *(volatile uint32_t *)&g_ps1_ram[(i * 64u * 37u) & 0x1FFFC0u];
    t1 = bench_t();
    for (i = 0; i < 16384u; i++) soma += *(volatile uint32_t *)&g_ps1_ram[(i * 4u) & 0x7FFCu];
    t2 = bench_t();
    printf("bench: leitura RAM do PS1: falta de cache %lu ciclos, sequencial %lu ciclos/100 (%lu)\n",
           (unsigned long)((t1 - t0) * 100u / 16384u), (unsigned long)((t2 - t1) * 100u / 16384u),
           (unsigned long)(soma & 1u));
    {   /* polígono típico: texturizado gouraud de 4 vértices, mesmo estado */
        GsrEstado e; GsrVertice v[4]; unsigned k;
        memset(&e, 0, sizeof(e)); e.area_fim = 1023u | (511u << 10); e.modo = 0x100u;
        for (k = 0; k < 4; k++) { v[k].x = 10 + k * 20; v[k].y = 10 + (k & 1) * 30; v[k].u = k * 8; v[k].v = k * 4;
                                  v[k].r = v[k].g = v[k].b = 0x80; }
        gsr_poligono(&e, v, 4, 0x3Cu, 0x7FC0u);   /* aquece o estado */
        fila_n = 0;
        t0 = bench_t();
        for (k = 0; k < 256u; k++) gsr_poligono(&e, v, 4, 0x3Cu, 0x7FC0u);
        t1 = bench_t();
        fila_n = 0;   /* descarta: nada vai para o GS */
        chave_valida = 0;
        printf("bench: gsr_poligono (4 vertices, textura, gouraud) %lu ciclos\n",
               (unsigned long)((t1 - t0) / 256u));
    }
    {   /* autoteste V29: caminho direto x caminho antigo, polígonos aleatórios */
        extern void ps2_gpu_gp0_bloco(const uint32_t *p, unsigned n);
        extern int g_ps2_gpu_pk;
        static uint32_t pk[16];
        static uint64_t ref[256];
        uint32_t sem = 777u; unsigned k, j, w, dif = 0, nref;
        for (k = 0; k < 3000u; k++) {
            uint32_t op;
            sem = sem * 1103515245u + 12345u; op = 0x20u + ((sem >> 16) & 31u);
            pk[0] = (op << 24) | (sem & 0xFFFFFFu);
            for (w = 1; w < 16u; w++) {
                sem = sem * 1103515245u + 12345u;
                pk[w] = (sem >> 4);
            }
            {   /* posições moderadas (às vezes grandes, para testar o descarte) */
                unsigned nv = (op & 8u) ? 4u : 3u, pi = 1;
                for (j = 0; j < nv; j++) {
                    if (j && (op & 16u)) pi++;
                    if (k % 5u) pk[pi] = ((pk[pi] & 0x1FFu) - 0x80u) | (((pk[pi] >> 16) & 0xFFu) << 16);
                    pi++;
                    if (op & 4u) { if (j == 1) pk[pi] = (pk[pi] & 0x00FFFFu) | ((pk[pi] & 0x09FF0000u)); pi++; }
                }
            }
            g_ps2_gpu_pk = 0;
            gsr_estado_perdido(); chave_valida = 0; fila_n = 0;
            ps2_gpu_gp0_bloco(pk, 16u > 0 ? (unsigned)((op & 8u) ? 4u : 3u) * (1u + !!(op & 4u)) + 1u + ((op & 16u) ? ((op & 8u) ? 3u : 2u) : 0u) : 0u);
            gsr_estado_perdido(); chave_valida = 0; fila_n = 0;
            ps2_gpu_gp0_bloco(pk, (unsigned)((op & 8u) ? 4u : 3u) * (1u + !!(op & 4u)) + 1u + ((op & 16u) ? ((op & 8u) ? 3u : 2u) : 0u));
            nref = fila_n * 2u; if (nref > 256u) nref = 256u;
            for (w = 0; w < nref; w++) ref[w] = UCAB(&fila[fila_atual][0])[w];
            g_ps2_gpu_pk = 1;
            gsr_estado_perdido(); chave_valida = 0; fila_n = 0;
            ps2_gpu_gp0_bloco(pk, (unsigned)((op & 8u) ? 4u : 3u) * (1u + !!(op & 4u)) + 1u + ((op & 16u) ? ((op & 8u) ? 3u : 2u) : 0u));
            if (fila_n * 2u != nref && nref < 256u) { if (dif++ < 4) printf("bench: gpu DIFERENTE op %02lx tamanho %lu x %lu\n", (unsigned long)op, (unsigned long)nref, (unsigned long)fila_n * 2ul); }
            else for (w = 0; w < nref; w++) if (UCAB(&fila[fila_atual][0])[w] != ref[w]) {
                if (dif++ < 4) printf("bench: gpu DIFERENTE op %02lx qword %u\n", (unsigned long)op, w);
                break;
            }
            fila_n = 0;
        }
        g_ps2_gpu_pk = dif == 0;
        gsr_estado_perdido(); chave_valida = 0;
        printf("bench: gpu autoteste direto x antigo: 3000 poligonos, %u diferencas (direto %s)\n",
               dif, g_ps2_gpu_pk ? "ligado" : "DESLIGADO");
    }
    {   /* caminho inteiro: comando GP0 0x3C (12 palavras) -> pacote do GS */
        extern void ps2_gpu_gp0_bloco(const uint32_t *p, unsigned n);
        static uint32_t pk[12 * 64];
        unsigned k, j;
        for (k = 0; k < 64u; k++) {
            uint32_t *q = &pk[k * 12u];
            q[0] = 0x3C808080u;
            for (j = 0; j < 4u; j++) {
                if (j) q[j * 3u] = 0x00808080u;
                q[j * 3u + 1u] = (uint32_t)(10 + j * 20) | ((uint32_t)(10 + (j & 1) * 30) << 16);
                q[j * 3u + 2u] = (j * 8u) | ((j * 4u) << 8) | (j == 0 ? 0x7FC00000u : j == 1 ? 0x00100000u : 0u);
            }
        }
        ps2_gpu_gp0_bloco(pk, 12u);
        fila_n = 0;
        t0 = bench_t();
        for (k = 0; k < 4u; k++) { ps2_gpu_gp0_bloco(pk, 12u * 64u); fila_n = 0; }
        t1 = bench_t();
        chave_valida = 0;
        printf("bench: comando GP0 de poligono inteiro (lista -> fila do GS) %lu ciclos\n",
               (unsigned long)((t1 - t0) / 256u));
        {
            extern int g_ps2_gpu_pk; int salvo = g_ps2_gpu_pk;
            g_ps2_gpu_pk = 0;
            t0 = bench_t();
            for (k = 0; k < 4u; k++) { ps2_gpu_gp0_bloco(pk, 12u * 64u); fila_n = 0; }
            t1 = bench_t();
            g_ps2_gpu_pk = salvo; chave_valida = 0;
            printf("bench: (caminho antigo: %lu ciclos)\n", (unsigned long)((t1 - t0) / 256u));
        }
    }
}
