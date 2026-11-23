/*
 * GPU do PS1 para o runtime do EE.
 * Especificação: PSX-SPX; referência do projeto: PSXRecomp (mstan),
 * PolyForm Noncommercial 1.0.0, e Tekken3Recompiled (FishB0nes98).
 */
#include "quente.h"
#include "gpu.h"
#include "ram.h"            /* ps2_u32a */

#include <stdio.h>
#include <string.h>

/* muda a cada envio CPU->VRAM e cópia VRAM->VRAM (io.c: redesenho igual) */
PS2_QUENTE uint32_t g_ps2_vram_geracao;

#ifdef _EE
#include "gs_render.h"
#define GS_ATIVO g_gsr_ativo
#else
#define GS_ATIVO 0
#endif

static uint16_t vram[PS2_GPU_LARGURA * PS2_GPU_ALTURA]
    __attribute__((aligned(64)));

typedef struct {
    uint32_t pacote[16], recebido, esperado;
    uint32_t modo, janela, area_ini, area_fim, offset, mascara;
    uint32_t display_x, display_y, display_h, display_v, display_mode;
    uint32_t display_off, dma_direction, leitura;
    uint32_t x, y, largura, altura, indice, total;
    uint32_t cx, cy;   /* V48: indice % largura e indice / largura (sem dividir a cada pixel) */
    int transferencia; /* 1: CPU -> VRAM; 2: VRAM -> CPU */
    uint32_t linha_op, linha_pos, linha_cor, linha_proxima_cor;
    int polilinha, linha_aguarda_pos;
} GPU;

static PS2_QUENTE GPU gpu;

static PS2_QUENTE int area_velha;      /* V50: a área de desenho atual já está marcada como velha na sombra */

#ifdef _EE
static void marcar_area_velha(void);
/* chamada em todo desenho que vai só para o GS */
static GsrEstado estado_gs(void)
{
    GsrEstado e;
    if (!area_velha) marcar_area_velha();
    e.modo = gpu.modo; e.janela = gpu.janela;
    e.area_ini = gpu.area_ini; e.area_fim = gpu.area_fim; e.mascara = gpu.mascara;
    return e;
}
#endif

/* Diagnóstico: pixels desenhados por tipo (zerado a cada relatório). */
unsigned long g_ps2_px_tri, g_ps2_px_tri_tex, g_ps2_px_ret, g_ps2_px_semi;
unsigned long g_ps2_px_ret_solido, g_ps2_px_ret_tex, g_ps2_px_ret_lento;
unsigned long g_ps2_px_ret_bruto;
unsigned long long g_ps2_ciclos_tri, g_ps2_ciclos_ret;

#ifdef PS2_DEBUG_GPU
static unsigned debug_quantidade, debug_ativo, debug_pixels;
#endif

static uint16_t *pixel(unsigned x, unsigned y)
{
    return &vram[(y & 511u) * PS2_GPU_LARGURA + (x & 1023u)];
}

static void escrever_pixel(unsigned x, unsigned y, uint16_t cor)
{
    uint16_t *destino = pixel(x, y);
    if ((gpu.mascara & 2u) && (*destino & 0x8000u)) return;
    *destino = (uint16_t)(cor | ((gpu.mascara & 1u) << 15));
#ifdef PS2_DEBUG_GPU
    if (debug_ativo) ++debug_pixels;
#endif
}

static uint32_t dimensao(uint32_t n, uint32_t mascara)
{
    return ((n - 1u) & mascara) + 1u;
}

static void transferencia_inicio(int modo, uint32_t pos, uint32_t tam)
{
    gpu.x = pos & 1023u;
    gpu.y = (pos >> 16) & 511u;
    gpu.largura = dimensao(tam & 0xffffu, 1023u);
    gpu.altura = dimensao(tam >> 16, 511u);
    gpu.indice = 0;
    gpu.cx = gpu.cy = 0;
    gpu.total = gpu.largura * gpu.altura;
    gpu.transferencia = modo;
}

/* V50: onde a sombra da VRAM está em dia (ver gpu.h). 1 bit por halfword;
 * linha y = sombra_ok[y * PS2_GPU_SOMBRA_PALAVRAS ...]. */
#define SP PS2_GPU_SOMBRA_PALAVRAS
static uint64_t sombra_ok[512u * SP];
/* VBlank da última escrita da CPU em cada grupo de 64 halfwords (envelhecer) */
static uint32_t escrita_vb[512u * SP];
static PS2_QUENTE uint32_t vb_contador;
const uint64_t *ps2_gpu_sombra_ok(void) { return sombra_ok; }

/* bits [x, x+n) de uma linha (com a volta em 1024) */
static void faixa_bits(uint64_t *lin, uint32_t x, uint32_t n, int em_dia)
{
    x &= 1023u;
    if (n > 1024u) n = 1024u;
    while (n) {
        uint32_t b = x & 63u, k = 64u - b;
        uint64_t m;
        if (k > n) k = n;
        m = k == 64u ? ~0ull : (((1ull << k) - 1ull) << b);
        if (em_dia) lin[x >> 6] |= m; else lin[x >> 6] &= ~m;
        n -= k;
        x = (x + k) & 1023u;
    }
}

/* o retângulo (com a volta da VRAM) toca a área de desenho? */
static int toca_area_desenho(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint32_t ax0 = gpu.area_ini & 1023u, ay0 = (gpu.area_ini >> 10) & 511u;
    uint32_t ax1 = gpu.area_fim & 1023u, ay1 = (gpu.area_fim >> 10) & 511u;
    x &= 1023u; y &= 511u;
    if (x + w > 1024u || y + h > 512u) return 1;          /* dá a volta: na dúvida, sim */
    return x <= ax1 && x + w > ax0 && y <= ay1 && y + h > ay0;
}

void ps2_gpu_sombra_marcar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int em_dia)
{
    uint32_t j;
    if (!w || !h) return;
    if (w > 1024u) w = 1024u;
    if (h > 512u) h = 512u;
    for (j = 0; j < h; j++) {
        uint32_t lin = ((y + j) & 511u) * SP;
        faixa_bits(&sombra_ok[lin], x, w, em_dia);
        if (em_dia) {
            uint32_t g0 = (x & 1023u) >> 6, ng = ((x & 63u) + w + 63u) >> 6, g;
            if (ng > SP) ng = SP;
            for (g = 0; g < ng; g++) escrita_vb[lin + ((g0 + g) & (SP - 1u))] = vb_contador;
        }
    }
    /* escrita na área de desenho: o próximo desenho só no GS a marca como
     * velha de novo (fora dela, como as texturas, não precisa) */
    if (em_dia && toca_area_desenho(x, y, w, h)) area_velha = 0;
}

int ps2_gpu_sombra_faixa(unsigned y, unsigned x, unsigned n)
{
    const uint64_t *lin = &sombra_ok[(y & 511u) * SP];
    int algum = 0, todos = 1;
    x &= 1023u;
    if (n > 1024u) n = 1024u;
    while (n) {
        unsigned b = x & 63u, k = 64u - b;
        uint64_t m, v;
        if (k > n) k = n;
        m = k == 64u ? ~0ull : (((1ull << k) - 1ull) << b);
        v = lin[x >> 6] & m;
        if (v) algum = 1;
        if (v != m) todos = 0;
        if (algum && !todos) return PS2_SOMBRA_MISTA;
        n -= k;
        x = (x + k) & 1023u;
    }
    return todos ? PS2_SOMBRA_EM_DIA : PS2_SOMBRA_VELHA;
}

void ps2_gpu_sombra_envelhecer(unsigned vblanks)
{
    uint32_t i;
    for (i = 0; i < 512u * SP; i++)
        if ((int32_t)(vb_contador - escrita_vb[i]) > (int32_t)vblanks) sombra_ok[i] = 0;
}

#ifdef _EE
/* Com o GS, polígonos, sprites e linhas vão só para o GS: a área de desenho
 * inteira fica velha na sombra (uma vez, até a área mudar ou ser escrita) */
static void marcar_area_velha(void)
{
    uint32_t ax0 = gpu.area_ini & 1023u, ay0 = (gpu.area_ini >> 10) & 511u;
    uint32_t ax1 = gpu.area_fim & 1023u, ay1 = (gpu.area_fim >> 10) & 511u;
    area_velha = 1;
    if (ax1 < ax0 || ay1 < ay0) return;
    {
        uint32_t j;
        for (j = ay0; j <= ay1; j++) faixa_bits(&sombra_ok[j * SP], ax0, ax1 - ax0 + 1u, 0);
    }
}
#endif

static void transferencia_fim(void)
{
    gpu.transferencia = 0;
    ps2_gpu_sombra_marcar(gpu.x, gpu.y, gpu.largura, gpu.altura, 1);
    g_ps2_vram_geracao++;
#ifdef _EE
    if (GS_ATIVO) gsr_enviar(vram, gpu.x, gpu.y, gpu.largura, gpu.altura);
#endif
}

static void transferencia_avancar(void)
{
    if (++gpu.cx == gpu.largura) { gpu.cx = 0; gpu.cy++; }
}

static void transferencia_escrever(uint16_t valor)
{
    escrever_pixel(gpu.x + gpu.cx, gpu.y + gpu.cy, valor);
    transferencia_avancar();
    if (++gpu.indice == gpu.total) transferencia_fim();
}

static uint16_t transferencia_ler(void)
{
    uint16_t valor = *pixel(gpu.x + gpu.cx, gpu.y + gpu.cy);
    transferencia_avancar();
    if (++gpu.indice == gpu.total) gpu.transferencia = 0;
    return valor;
}

/* V48: imagem CPU -> VRAM em bloco (DMA2 em blocos, LoadImage). Antes cada
 * pixel passava por ps2_gpu_gp0 e calculava a posição com divisão e resto
 * (~2 divisões por pixel: o vídeo da abertura manda 16 faixas de 24x224 por
 * quadro). Mesmo resultado do caminho de 1 palavra: metade baixa primeiro e,
 * se a imagem acabar no meio da palavra, a metade alta é descartada.
 * Devolve quantas palavras de p consumiu (0 se não há transferência). */
extern unsigned long g_ps2_gpu_palavras;
unsigned ps2_gpu_gp0_imagem(const void *dados, unsigned n)
{
    unsigned usadas = 0;
#ifdef PS2_DEBUG_GPU
    (void)dados; (void)n;
    return 0;
#else
    /* lido como palavras de um vetor de bytes (g_ps1_ram): may_alias.
     * dados precisa estar alinhado a 4 bytes (gpu.h). */
    const ps2_u32a *p = (const ps2_u32a *)dados;
    const uint16_t bit = (uint16_t)((gpu.mascara & 1u) << 15);
    const int checar = (gpu.mascara & 2u) != 0u;
    while (usadas < n && gpu.transferencia == 1) {
        /* pixels até o fim da linha atual, em pares (palavras inteiras) */
        uint32_t resto_linha = gpu.largura - gpu.cx;
        uint32_t resto_total = gpu.total - gpu.indice;
        uint32_t k = resto_linha < resto_total ? resto_linha : resto_total;
        uint16_t *linha = &vram[((gpu.y + gpu.cy) & 511u) * PS2_GPU_LARGURA];
        uint32_t x = gpu.x + gpu.cx, i;
        if (k < 2u) {
            /* fim de linha no meio de uma palavra: caminho de 1 palavra */
            ps2_gpu_gp0(p[usadas++]);
            continue;
        }
        k &= ~1u;                                   /* pares inteiros nesta linha */
        if (k / 2u > n - usadas) k = (n - usadas) * 2u;
        if (!checar && (x & 1023u) + k <= 1024u) {
            /* caso comum (vídeo, texturas): sem máscara e sem dar a volta em x */
            uint16_t *d = &linha[x & 1023u];
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            if (!bit) {                             /* cópia pura: metade baixa primeiro */
                memcpy(d, (const void *)&p[usadas], k * 2u);
                usadas += k / 2u;
            } else
#endif
            for (i = 0; i < k; i += 2u) {
                uint32_t w = p[usadas++];
                d[i] = (uint16_t)((uint16_t)w | bit);
                d[i + 1u] = (uint16_t)((uint16_t)(w >> 16) | bit);
            }
        } else {
            for (i = 0; i < k; i += 2u) {
                uint32_t w = p[usadas++];
                uint16_t *d0 = &linha[(x + i) & 1023u], *d1 = &linha[(x + i + 1u) & 1023u];
                if (!checar || !(*d0 & 0x8000u)) *d0 = (uint16_t)((uint16_t)w | bit);
                if (!checar || !(*d1 & 0x8000u)) *d1 = (uint16_t)((uint16_t)(w >> 16) | bit);
            }
        }
        g_ps2_gpu_palavras += k / 2u;
        gpu.cx += k; gpu.indice += k;
        if (gpu.cx == gpu.largura) { gpu.cx = 0; gpu.cy++; }
        if (gpu.indice == gpu.total) transferencia_fim();
    }
    return usadas;
#endif
}

static uint16_t cor15(uint32_t cor)
{
    return (uint16_t)(((cor >> 3) & 31u) |
                      ((cor >> 6) & 0x3e0u) |
                      ((cor >> 9) & 0x7c00u));
}

typedef struct {
    int x, y, u, v;
    int r, g, b;
} Vertice;

static int sinal11(uint32_t n)
{
    n &= 2047u;
    return (n & 1024u) ? (int)n - 2048 : (int)n;
}

static Vertice vertice(uint32_t pos, uint32_t cor, uint32_t uv)
{
    Vertice v;
    v.x = sinal11(pos) + sinal11(gpu.offset);
    v.y = sinal11(pos >> 16) + sinal11(gpu.offset >> 11);
    v.u = (int)(uv & 255u); v.v = (int)((uv >> 8) & 255u);
    v.r = (int)(cor & 255u); v.g = (int)((cor >> 8) & 255u);
    v.b = (int)((cor >> 16) & 255u);
    return v;
}

#ifdef PS2_DEBUG_GPU
static void debug_inicio(void)
{
    debug_ativo = debug_quantidade < 300u;
    debug_pixels = 0;
}

static void debug_fim(unsigned op, const Vertice *v, unsigned n, uint32_t clut,
                      int ignora_area)
{
    unsigned i;
    int minx = v[0].x, maxx = v[0].x, miny = v[0].y, maxy = v[0].y;
    int fora, grande;
    if (!debug_ativo) return;
    for (i = 1; i < n; ++i) {
        if (v[i].x < minx) minx = v[i].x;
        if (v[i].x > maxx) maxx = v[i].x;
        if (v[i].y < miny) miny = v[i].y;
        if (v[i].y > maxy) maxy = v[i].y;
    }
    fora = !ignora_area &&
        (maxx < (int)(gpu.area_ini & 1023u) ||
         minx > (int)(gpu.area_fim & 1023u) ||
         maxy < (int)((gpu.area_ini >> 10) & 1023u) ||
         miny > (int)((gpu.area_fim >> 10) & 1023u) ||
         maxx < 0 || minx >= 1024 || maxy < 0 || miny >= 512);
    grande = op >= 0x20 && op <= 0x5f &&
             (maxx - minx > 1023 || maxy - miny > 511);
    printf("gpu dbg: #%u op=%02x modo=%04lx clut=%04lx area=(%lu,%lu)-(%lu,%lu) fora=%d grande=%d pixels=%u",
           ++debug_quantidade, op, (unsigned long)gpu.modo, (unsigned long)clut,
           (unsigned long)(gpu.area_ini & 1023u),
           (unsigned long)((gpu.area_ini >> 10) & 1023u),
           (unsigned long)(gpu.area_fim & 1023u),
           (unsigned long)((gpu.area_fim >> 10) & 1023u),
           fora, grande, debug_pixels);
    for (i = 0; i < n; ++i)
        printf(" v%u=(%d,%d) rgb=%02x%02x%02x uv=(%d,%d)", i,
               v[i].x, v[i].y, v[i].r, v[i].g, v[i].b, v[i].u, v[i].v);
    printf("\n");
    debug_ativo = 0;
}
#endif

static int limitar(int n, int maximo)
{
    return n < 0 ? 0 : n > maximo ? maximo : n;
}

static uint16_t textura(int u, int v, uint32_t clut)
{
    unsigned mx = (gpu.janela & 31u) << 3;
    unsigned my = ((gpu.janela >> 5) & 31u) << 3;
    unsigned ox = ((gpu.janela >> 10) & 31u) << 3;
    unsigned oy = ((gpu.janela >> 15) & 31u) << 3;
    unsigned tx = (gpu.modo & 15u) * 64u;
    unsigned ty = ((gpu.modo >> 4) & 1u) * 256u;
    unsigned profundidade = (gpu.modo >> 7) & 3u;
    unsigned valor, indice;
    u = (int)(((unsigned)u & 255u & ~mx) | (ox & mx));
    v = (int)(((unsigned)v & 255u & ~my) | (oy & my));
    if (profundidade >= 2u) return *pixel(tx + (unsigned)u, ty + (unsigned)v);
    valor = *pixel(tx + ((unsigned)u >> (profundidade ? 1 : 2)), ty + (unsigned)v);
    indice = profundidade ? ((valor >> ((u & 1) * 8)) & 255u)
                          : ((valor >> ((u & 3) * 4)) & 15u);
    return *pixel(((clut & 63u) << 4) + indice, (clut >> 6) & 511u);
}

static int mistura(int fundo, int frente, unsigned modo)
{
    switch (modo) {
    case 0: return (fundo >> 1) + (frente >> 1);
    case 1: return limitar(fundo + frente, 31);
    case 2: return limitar(fundo - frente, 31);
    default: return limitar(fundo + (frente >> 2), 31);
    }
}

/* Pixel já recortado (dentro da área de desenho e da VRAM). */
static void pixel_final(int x, int y, int r, int g, int b, int u, int v,
                        unsigned op, uint32_t clut)
{
    uint16_t tex = 0, fundo, cor;
    int tr, tg, tb;
    if (op & 4u) {
        tex = textura(u, v, clut);
        if (tex == 0) return;
        tr = tex & 31; tg = (tex >> 5) & 31; tb = (tex >> 10) & 31;
        if (!(op & 1u)) {
            tr = limitar((tr * r) >> 7, 31);
            tg = limitar((tg * g) >> 7, 31);
            tb = limitar((tb * b) >> 7, 31);
        }
    } else {
        tr = limitar(r, 255) >> 3;
        tg = limitar(g, 255) >> 3;
        tb = limitar(b, 255) >> 3;
    }
    if ((op & 2u) && (!(op & 4u) || (tex & 0x8000u))) {
        unsigned modo = (gpu.modo >> 5) & 3u;
        fundo = *pixel((unsigned)x, (unsigned)y);
        tr = mistura(fundo & 31, tr, modo);
        tg = mistura((fundo >> 5) & 31, tg, modo);
        tb = mistura((fundo >> 10) & 31, tb, modo);
    }
    cor = (uint16_t)(tr | (tg << 5) | (tb << 10) | (tex & 0x8000u));
    escrever_pixel((unsigned)x, (unsigned)y, cor);
}

static void desenhar_pixel(int x, int y, int r, int g, int b, int u, int v,
                           unsigned op, uint32_t clut)
{
    if (x < (int)(gpu.area_ini & 1023u) || x > (int)(gpu.area_fim & 1023u) ||
        y < (int)((gpu.area_ini >> 10) & 1023u) ||
        y > (int)((gpu.area_fim >> 10) & 1023u) ||
        x < 0 || x >= 1024 || y < 0 || y >= 512) return;
    pixel_final(x, y, r, g, b, u, v, op, clut);
}

static int64_t borda(const Vertice *a, const Vertice *b, int x, int y)
{
    return (int64_t)(b->x - a->x) * (y - a->y) -
           (int64_t)(b->y - a->y) * (x - a->x);
}

static int64_t borda_centro(const Vertice *a, const Vertice *b, int x, int y)
{
    return 2 * borda(a, b, x, y) + (b->x - a->x) - (b->y - a->y);
}

static int borda_incluida(const Vertice *a, const Vertice *b)
{
    return b->y < a->y || (b->y == a->y && b->x > a->x);
}

/*
 * Triângulo por funções de borda, de forma INCREMENTAL (otimizado):
 *  - as três bordas (com o critério de "centro do pixel" e a regra de
 *    inclusão topo-esquerda) são atualizadas com somas por pixel/linha;
 *  - cor e UV são interpoladas em ponto fixo 16.16: as divisões só
 *    acontecem uma vez por linha, não por pixel;
 *  - flat (sem Gouraud) e sem textura pulam as interpolações;
 *  - o recorte é feito uma vez (caixa x área de desenho x VRAM).
 * A cobertura de pixels é a mesma da versão anterior (por pixel).
 */
typedef struct { int64_t w, dx, dy; int incl; } Borda;

static Borda borda_init(const Vertice *a, const Vertice *b, int x0, int y0)
{
    Borda e;
    e.w = borda_centro(a, b, x0, y0);
    e.dx = -2 * (int64_t)(b->y - a->y);
    e.dy = 2 * (int64_t)(b->x - a->x);
    e.incl = borda_incluida(a, b);
    return e;
}

static inline int dentro(int64_t w, int incl)
{
    return w > 0 || (w == 0 && incl);
}

/* valor 16.16 de um atributo no ponto (wa,wb,wc) */
static inline int64_t atributo(int64_t wa, int64_t wb, int64_t wc,
                               int ca, int cb, int cc, int64_t div)
{
    return ((wa * ca + wb * cb + wc * cc) * 65536) / div;
}

static void triangulo(Vertice a, Vertice b, Vertice c, unsigned op, uint32_t clut)
{
    int minx = a.x, maxx = a.x, miny = a.y, maxy = a.y, x, y;
    int ax0, ay0, ax1, ay1;
    int gouraud = (op & 0x10u) != 0, tex = (op & 4u) != 0;
    int64_t area = borda(&a, &b, c.x, c.y), div;
    Borda ea, eb, ec;
    int64_t dr = 0, dg = 0, dbl = 0, du = 0, dv = 0;   /* gradientes 16.16 por x */

    if (!area) return;
    if (area < 0) { Vertice t = b; b = c; c = t; area = -area; }
    if (b.x < minx) minx = b.x;
    if (c.x < minx) minx = c.x;
    if (b.x > maxx) maxx = b.x;
    if (c.x > maxx) maxx = c.x;
    if (b.y < miny) miny = b.y;
    if (c.y < miny) miny = c.y;
    if (b.y > maxy) maxy = b.y;
    if (c.y > maxy) maxy = c.y;
    if (maxx - minx > 1023 || maxy - miny > 511) return;
    ax0 = (int)(gpu.area_ini & 1023u); ay0 = (int)((gpu.area_ini >> 10) & 1023u);
    ax1 = (int)(gpu.area_fim & 1023u); ay1 = (int)((gpu.area_fim >> 10) & 1023u);
    if (minx < ax0) minx = ax0;
    if (miny < ay0) miny = ay0;
    if (maxx > ax1) maxx = ax1;
    if (maxy > ay1) maxy = ay1;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > 1023) maxx = 1023;
    if (maxy > 511) maxy = 511;
    if (minx > maxx || miny > maxy) return;

    div = area * 2;
    ea = borda_init(&b, &c, minx, miny);
    eb = borda_init(&c, &a, minx, miny);
    ec = borda_init(&a, &b, minx, miny);
    if (gouraud) {
        dr  = ((ea.dx * a.r + eb.dx * b.r + ec.dx * c.r) * 65536) / div;
        dg  = ((ea.dx * a.g + eb.dx * b.g + ec.dx * c.g) * 65536) / div;
        dbl = ((ea.dx * a.b + eb.dx * b.b + ec.dx * c.b) * 65536) / div;
    }
    if (tex) {
        du = ((ea.dx * a.u + eb.dx * b.u + ec.dx * c.u) * 65536) / div;
        dv = ((ea.dx * a.v + eb.dx * b.v + ec.dx * c.v) * 65536) / div;
    }

    for (y = miny; y <= maxy; ++y) {
        int64_t wa = ea.w, wb = eb.w, wc = ec.w;
        int64_t r = 0, g = 0, bl = 0, u = 0, v = 0;
        int inicio = 1;
        for (x = minx; x <= maxx; ++x, wa += ea.dx, wb += eb.dx, wc += ec.dx) {
            if (!dentro(wa, ea.incl) || !dentro(wb, eb.incl) || !dentro(wc, ec.incl)) {
                if (!inicio) break;          /* saiu do triângulo: fim da linha */
                continue;
            }
            if (inicio) {                    /* 1º pixel da linha: valores exatos */
                inicio = 0;
                if (gouraud) {
                    r  = atributo(wa, wb, wc, a.r, b.r, c.r, div);
                    g  = atributo(wa, wb, wc, a.g, b.g, c.g, div);
                    bl = atributo(wa, wb, wc, a.b, b.b, c.b, div);
                }
                if (tex) {
                    u = atributo(wa, wb, wc, a.u, b.u, c.u, div);
                    v = atributo(wa, wb, wc, a.v, b.v, c.v, div);
                }
            }
            g_ps2_px_tri++;
            if (tex) g_ps2_px_tri_tex++;
            if (op & 2u) g_ps2_px_semi++;
            pixel_final(x, y,
                        gouraud ? (int)(r >> 16) : a.r,
                        gouraud ? (int)(g >> 16) : a.g,
                        gouraud ? (int)(bl >> 16) : a.b,
                        (int)(u >> 16), (int)(v >> 16), op, clut);
            r += dr; g += dg; bl += dbl; u += du; v += dv;
        }
        ea.w += ea.dy; eb.w += eb.dy; ec.w += ec.dy;
    }
}

static void poligono(unsigned op)
{
    Vertice v[4];
    unsigned n = (op & 8u) ? 4u : 3u, i, p = 1;
    uint32_t cor = gpu.pacote[0], clut = 0;
    int minx, maxx, miny, maxy;
    for (i = 0; i < n; ++i) {
        uint32_t pos, uv = 0;
        if (i && (op & 16u)) cor = gpu.pacote[p++];
        pos = gpu.pacote[p++];
        if (op & 4u) {
            uv = gpu.pacote[p++];
            if (i == 0) clut = uv >> 16;
            if (i == 1) gpu.modo = (gpu.modo & ~0x9ffu) | ((uv >> 16) & 0x9ffu);
        }
        v[i] = vertice(pos, cor, uv);
    }
    minx = maxx = v[0].x;
    miny = maxy = v[0].y;
    for (i = 1; i < n; ++i) {
        if (v[i].x < minx) minx = v[i].x;
        if (v[i].x > maxx) maxx = v[i].x;
        if (v[i].y < miny) miny = v[i].y;
        if (v[i].y > maxy) maxy = v[i].y;
    }
#ifdef PS2_DEBUG_GPU
    debug_inicio();
#endif
    if (maxx - minx <= 1023 && maxy - miny <= 511) {
#ifdef _EE
        if (GS_ATIVO) {
            GsrEstado e = estado_gs();
            gsr_poligono(&e, (const GsrVertice *)v, (int)n, op, clut);
            return;
        }
#endif
        triangulo(v[0], v[1], v[2], op, clut);
        if (n == 4) triangulo(v[1], v[2], v[3], op, clut);
    }
#ifdef PS2_DEBUG_GPU
    debug_fim(op, v, n, clut, 0);
#endif
}

/*
 * Retângulo/sprite texturizado, caminho rápido (sem janela de textura e
 * sem espelhamento): linha da textura e CLUT acessadas por ponteiro, sem
 * recalcular página/janela por pixel. Mesmo resultado de pixel_final.
 */
static void ret_texturizado(int cx0, int cy0, int cx1, int cy1, int x0, int y0,
                            const Vertice *a, unsigned op, uint32_t clut)
{
    unsigned tx = (gpu.modo & 15u) * 64u, ty = ((gpu.modo >> 4) & 1u) * 256u;
    unsigned prof = (gpu.modo >> 7) & 3u, modo_semi = (gpu.modo >> 5) & 3u;
    unsigned cx = (clut & 63u) << 4, cy = (clut >> 6) & 511u;
    int bruto = (op & 1u) || (a->r == 128 && a->g == 128 && a->b == 128);
    uint16_t mask_set = (uint16_t)((gpu.mascara & 1u) << 15);
    int mask_chk = (gpu.mascara & 2u) != 0;
    int yy, xx;

    /* Caso mais comum: cor da textura sem modulação, opaco, sem máscara.
     * Um laço por profundidade, sem decisões por pixel além de "texel 0". */
    if (bruto && !(op & 2u) && !mask_chk && !mask_set) {
        g_ps2_px_ret_bruto += (unsigned long)(cx1 - cx0 + 1) * (unsigned long)(cy1 - cy0 + 1);
        const uint16_t *lc = &vram[cy * 1024u];
        for (yy = cy0; yy <= cy1; ++yy) {
            unsigned v = (unsigned)(a->v + (yy - y0)) & 255u;
            const uint16_t *lt = &vram[((ty + v) & 511u) * 1024u];
            uint16_t *ld = &vram[(unsigned)yy * 1024u];
            unsigned u = (unsigned)(a->u + (cx0 - x0));
            if (prof >= 2u) {          /* 15 bits (3 = reservado, igual a 15) */
                for (xx = cx0; xx <= cx1; ++xx, ++u) {
                    uint16_t t = lt[(tx + (u & 255u)) & 1023u];
                    if (t) ld[xx] = t;
                }
            } else if (prof == 1u) {
                for (xx = cx0; xx <= cx1; ++xx, ++u) {
                    unsigned uu = u & 255u;
                    uint16_t t = lc[(cx + ((lt[(tx + (uu >> 1)) & 1023u] >> ((uu & 1u) * 8u)) & 255u)) & 1023u];
                    if (t) ld[xx] = t;
                }
            } else {
                for (xx = cx0; xx <= cx1; ++xx, ++u) {
                    unsigned uu = u & 255u;
                    uint16_t t = lc[(cx + ((lt[(tx + (uu >> 2)) & 1023u] >> ((uu & 3u) * 4u)) & 15u)) & 1023u];
                    if (t) ld[xx] = t;
                }
            }
#ifdef PS2_DEBUG_GPU
            if (debug_ativo) debug_pixels += (unsigned)(cx1 - cx0 + 1);
#endif
        }
        return;
    }

    for (yy = cy0; yy <= cy1; ++yy) {
        unsigned v = (unsigned)(a->v + (yy - y0)) & 255u;
        const uint16_t *lt = &vram[((ty + v) & 511u) * 1024u];
        uint16_t *ld = &vram[(unsigned)yy * 1024u];
        for (xx = cx0; xx <= cx1; ++xx) {
            unsigned u = (unsigned)(a->u + (xx - x0)) & 255u;
            uint16_t tex, d;
            int tr, tg, tb;
            if (prof == 0u) {
                unsigned val = lt[(tx + (u >> 2)) & 1023u];
                tex = vram[cy * 1024u + ((cx + ((val >> ((u & 3u) * 4u)) & 15u)) & 1023u)];
            } else if (prof == 1u) {
                unsigned val = lt[(tx + (u >> 1)) & 1023u];
                tex = vram[cy * 1024u + ((cx + ((val >> ((u & 1u) * 8u)) & 255u)) & 1023u)];
            } else {
                tex = lt[(tx + u) & 1023u];
            }
            if (!tex) continue;
            d = ld[xx];
            if (mask_chk && (d & 0x8000u)) continue;
            tr = tex & 31; tg = (tex >> 5) & 31; tb = (tex >> 10) & 31;
            if (!bruto) {
                tr = limitar((tr * a->r) >> 7, 31);
                tg = limitar((tg * a->g) >> 7, 31);
                tb = limitar((tb * a->b) >> 7, 31);
            }
            if ((op & 2u) && (tex & 0x8000u)) {
                tr = mistura(d & 31, tr, modo_semi);
                tg = mistura((d >> 5) & 31, tg, modo_semi);
                tb = mistura((d >> 10) & 31, tb, modo_semi);
            }
            ld[xx] = (uint16_t)(tr | (tg << 5) | (tb << 10) | (tex & 0x8000u) | mask_set);
#ifdef PS2_DEBUG_GPU
            if (debug_ativo) ++debug_pixels;
#endif
        }
    }
}

static void retangulo(unsigned op)
{
    unsigned p = 2, x, y, w, h, clut = 0;
    uint32_t uv = 0;
    Vertice a;
    if (op & 4u) { uv = gpu.pacote[p++]; clut = uv >> 16; }
    switch ((op >> 3) & 3u) {
    case 0: w = gpu.pacote[p] & 1023u; h = (gpu.pacote[p] >> 16) & 511u; break;
    case 1: w = h = 1; break;
    case 2: w = h = 8; break;
    default: w = h = 16; break;
    }
    a = vertice(gpu.pacote[1], gpu.pacote[0], uv);
#ifdef _EE
    if (GS_ATIVO) {
        GsrEstado e = estado_gs();
        gsr_sprite(&e, (const GsrVertice *)&a, (int)w, (int)h, op, clut);
        return;
    }
#endif
#ifdef PS2_DEBUG_GPU
    debug_inicio();
#endif
    {
        /* Recorta UMA vez contra a área de desenho e a VRAM (antes era
         * conferido em cada pixel: 95% do tempo do port ia para isto). */
        int ax0 = (int)(gpu.area_ini & 1023u), ay0 = (int)((gpu.area_ini >> 10) & 1023u);
        int ax1 = (int)(gpu.area_fim & 1023u), ay1 = (int)((gpu.area_fim >> 10) & 1023u);
        int x0 = a.x, y0 = a.y, x1 = a.x + (int)w - 1, y1 = a.y + (int)h - 1;
        int cx0, cy0, cx1, cy1;
        if (ax1 > 1023) ax1 = 1023;
        if (ay1 > 511) ay1 = 511;
        cx0 = x0 > ax0 ? x0 : ax0; cy0 = y0 > ay0 ? y0 : ay0;
        cx1 = x1 < ax1 ? x1 : ax1; cy1 = y1 < ay1 ? y1 : ay1;
        if (cx0 < 0) cx0 = 0;
        if (cy0 < 0) cy0 = 0;
        if (w && h && cx0 <= cx1 && cy0 <= cy1) {
            g_ps2_px_ret += (unsigned long)(cx1 - cx0 + 1) * (unsigned long)(cy1 - cy0 + 1);
            unsigned long npx = (unsigned long)(cx1 - cx0 + 1) * (unsigned long)(cy1 - cy0 + 1);
            if (!(op & 6u)) g_ps2_px_ret_solido += npx;
            else if ((op & 4u) && gpu.janela == 0u && !(gpu.modo & 0x3000u)) g_ps2_px_ret_tex += npx;
            else g_ps2_px_ret_lento += npx;
            if (!(op & 6u)) {
                /* Caminho rápido: cor sólida, sem textura e sem transparência */
                uint16_t cor = (uint16_t)((limitar(a.r, 255) >> 3) |
                                          ((limitar(a.g, 255) >> 3) << 5) |
                                          ((limitar(a.b, 255) >> 3) << 10) |
                                          ((gpu.mascara & 1u) << 15));
                int yy, xx;
                for (yy = cy0; yy <= cy1; ++yy) {
                    uint16_t *linha_vram = pixel((unsigned)cx0, (unsigned)yy);
                    if (gpu.mascara & 2u) {
                        for (xx = 0; xx <= cx1 - cx0; ++xx)
                            if (!(linha_vram[xx] & 0x8000u)) linha_vram[xx] = cor;
                    } else {
                        for (xx = 0; xx <= cx1 - cx0; ++xx) linha_vram[xx] = cor;
                    }
#ifdef PS2_DEBUG_GPU
                    if (debug_ativo) debug_pixels += (unsigned)(cx1 - cx0 + 1);
#endif
                }
            } else if ((op & 4u) && gpu.janela == 0u && !(gpu.modo & 0x3000u)) {
                ret_texturizado(cx0, cy0, cx1, cy1, x0, y0, &a, op, clut);
            } else {
                int yy, xx;
                for (yy = cy0; yy <= cy1; ++yy) {
                    int dy = yy - y0;
                    for (xx = cx0; xx <= cx1; ++xx) {
                        int dx = xx - x0;
                        desenhar_pixel(xx, yy, a.r, a.g, a.b,
                                       a.u + ((gpu.modo & 0x1000u) ? -dx : dx),
                                       a.v + ((gpu.modo & 0x2000u) ? -dy : dy),
                                       op, clut);
                    }
                }
            }
        }
        (void)x; (void)y;
    }
#ifdef PS2_DEBUG_GPU
    {
        Vertice extremos[2] = {a, a};
        extremos[1].x += w ? (int)w - 1 : 0;
        extremos[1].y += h ? (int)h - 1 : 0;
        debug_fim(op, extremos, 2, clut, 0);
    }
#endif
}

static void linha(Vertice a, Vertice b, unsigned op)
{
    int dx = b.x - a.x, dy = b.y - a.y, passos, i;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
#ifdef PS2_DEBUG_GPU
    Vertice extremos[2] = {a, b};
    debug_inicio();
#endif
    if (ax > 1023 || ay > 511) {
#ifdef PS2_DEBUG_GPU
        debug_fim(op, extremos, 2, 0, 0);
#endif
        return;
    }
#ifdef _EE
    if (GS_ATIVO) {
        GsrEstado e = estado_gs();
        gsr_linha(&e, (const GsrVertice *)&a, (const GsrVertice *)&b, op);
        return;
    }
#endif
    passos = ax > ay ? ax : ay;
    if (!passos) passos = 1;
    for (i = 0; i <= passos; ++i)
        desenhar_pixel(a.x + dx * i / passos, a.y + dy * i / passos,
                       a.r + (b.r - a.r) * i / passos,
                       a.g + (b.g - a.g) * i / passos,
                       a.b + (b.b - a.b) * i / passos, 0, 0, op & ~4u, 0);
#ifdef PS2_DEBUG_GPU
    debug_fim(op, extremos, 2, 0, 0);
#endif
}

static void preencher(void)
{
    uint32_t x = gpu.pacote[1] & 0x3f0u;
    uint32_t y = (gpu.pacote[1] >> 16) & 511u;
    uint32_t w = ((gpu.pacote[2] & 1023u) + 15u) & ~15u;
    uint32_t h = (gpu.pacote[2] >> 16) & 511u;
    uint32_t i, j;
    uint16_t c = cor15(gpu.pacote[0]);
    int sombra = 1;
#ifdef PS2_DEBUG_GPU
    Vertice extremos[2] = {{0}};
    extremos[0].x = (int)x; extremos[0].y = (int)y;
    extremos[0].r = gpu.pacote[0] & 255u;
    extremos[0].g = (gpu.pacote[0] >> 8) & 255u;
    extremos[0].b = (gpu.pacote[0] >> 16) & 255u;
    extremos[1] = extremos[0];
    extremos[1].x += w ? (int)w - 1 : 0;
    extremos[1].y += h ? (int)h - 1 : 0;
    debug_inicio();
#endif
#ifdef _EE
    /* Com o GS a imagem é desenhada lá; a sombra em RAM só serve para montar
     * texturas de 4/8 bits. Limpar a tela (368x480) escrevia ~350 KB por
     * quadro na RAM (auditoria V19 2.1): só escreve onde já houve textura. */
    if (GS_ATIVO) {
        /* V50: dá a volta na VRAM como na sombra (o gsr_preencher corta na
         * borda): até 4 pedaços */
        uint32_t w1 = x + w > 1024u ? 1024u - x : w, h1 = y + h > 512u ? 512u - y : h;
        gsr_preencher(x, y, w1, h1, c);
        if (w1 < w) gsr_preencher(0, y, w - w1, h1, c);
        if (h1 < h) gsr_preencher(x, 0, w1, h - h1, c);
        if (w1 < w && h1 < h) gsr_preencher(0, 0, w - w1, h - h1, c);
        sombra = gsr_area_com_textura(x, y, w, h);
    }
#endif
    if (sombra)
        for (j = 0; j < h; ++j)
            for (i = 0; i < w; ++i)
                *pixel(x + i, y + j) = c;
    ps2_gpu_sombra_marcar(x, y, w, h, sombra);     /* V50 */
#ifdef PS2_DEBUG_GPU
    if (debug_ativo) debug_pixels = w * h;
    debug_fim(0x02, extremos, 2, 0, 1);
#endif
}

static void copiar_vram(void)
{
    uint32_t sx = gpu.pacote[1] & 1023u;
    uint32_t sy = (gpu.pacote[1] >> 16) & 511u;
    uint32_t dx = gpu.pacote[2] & 1023u;
    uint32_t dy = (gpu.pacote[2] >> 16) & 511u;
    uint32_t w = dimensao(gpu.pacote[3] & 0xffffu, 1023u);
    uint32_t h = dimensao(gpu.pacote[3] >> 16, 511u);
    uint32_t ii, jj, j;
    /* V50: com o GS, origem e destino sobrepostos podem ser copiados em
     * ordem diferente na sombra e no GS: o destino fica velho */
    int sobrepoe = GS_ATIVO &&
                   (((dx - sx) & 1023u) < w || ((sx - dx) & 1023u) < w) &&
                   (((dy - sy) & 511u) < h || ((sy - dy) & 511u) < h);
    /* V51: ordem da cópia igual à do GS (gsr_copiar, TRXPOS.DIR): de baixo
     * para cima se o destino está abaixo, da direita se está à direita. Com
     * sobreposição as duas cópias dão o mesmo resultado (as texturas de 4/8
     * bits saem da sombra) */
    for (jj = 0; jj < h; ++jj) {
        uint32_t j = dy > sy ? h - 1u - jj : jj;
        const uint64_t *so = &sombra_ok[((sy + j) & 511u) * SP];
        uint64_t *sd = &sombra_ok[((dy + j) & 511u) * SP];
        for (ii = 0; ii < w; ++ii) {
            uint32_t i = dx > sx ? w - 1u - ii : ii;
            /* V50: o destino fica em dia só onde a origem estava */
            uint32_t ox = (sx + i) & 1023u, qx = (dx + i) & 1023u;
            uint16_t *dest = pixel(dx + i, dy + j);
            uint64_t bit = 1ull << (qx & 63u);
            int ok = (int)((so[ox >> 6] >> (ox & 63u)) & 1u);
            if ((gpu.mascara & 2u) && (*dest & 0x8000u)) {
                /* bloqueado pela máscara: na sombra não muda; o GS
                 * (gsr_copiar) não testa a máscara e muda */
                if (GS_ATIVO) sd[qx >> 6] &= ~bit;
                continue;
            }
            escrever_pixel(dx + i, dy + j, *pixel(sx + i, sy + j));
            if (ok && !sobrepoe) sd[qx >> 6] |= bit; else sd[qx >> 6] &= ~bit;
        }
    }
    {   /* carimbo de escrita (envelhecer) e a área de desenho volta a ser
         * marcada no próximo desenho só no GS */
        uint32_t g0 = dx >> 6, ng = ((dx & 63u) + w + 63u) >> 6, g;
        if (ng > SP) ng = SP;
        for (j = 0; j < h; ++j)
            for (g = 0; g < ng; g++) escrita_vb[((dy + j) & 511u) * SP + ((g0 + g) & (SP - 1u))] = vb_contador;
        if (toca_area_desenho(dx, dy, w, h)) area_velha = 0;
    }
    g_ps2_vram_geracao++;
#ifdef _EE
    if (GS_ATIVO) gsr_copiar(sx, sy, dx, dy, w, h);
#endif
}

static void executar(void)
{
    uint32_t op = gpu.pacote[0] >> 24;
    if (op >= 0x20 && op <= 0x3f) { poligono(op); return; }
    if (op >= 0x40 && op <= 0x5f) {
        uint32_t cor2 = (op & 16u) ? gpu.pacote[2] : gpu.pacote[0];
        uint32_t pos2 = gpu.pacote[(op & 16u) ? 3 : 2];
        linha(vertice(gpu.pacote[1], gpu.pacote[0], 0), vertice(pos2, cor2, 0), op);
        if (op & 8u) {
            gpu.polilinha = 1;
            gpu.linha_op = op;
            gpu.linha_pos = pos2;
            gpu.linha_cor = cor2;
            gpu.linha_aguarda_pos = 0;
        }
        return;
    }
    if (op >= 0x60 && op <= 0x7f) { retangulo(op); return; }
    if (op >= 0x80 && op <= 0x9f) { copiar_vram(); return; }
    if (op >= 0xa0 && op <= 0xbf) {
        transferencia_inicio(1, gpu.pacote[1], gpu.pacote[2]); return;
    }
    if (op >= 0xc0 && op <= 0xdf) {
        transferencia_inicio(2, gpu.pacote[1], gpu.pacote[2]); return;
    }
    switch (op) {
    case 0x00: case 0x01: break;
    case 0x02: preencher(); break;
    case 0xe1: gpu.modo = gpu.pacote[0] & 0x3fffu; break;
    case 0xe2: gpu.janela = gpu.pacote[0] & 0xfffffu; break;
    /* área/deslocamento de desenho: se mudar, o mesmo conteúdo de lista
     * desenha em outro lugar (io.c não pode pular o "redesenho igual") */
    case 0xe3: { uint32_t v = gpu.pacote[0] & 0xfffffu;
                 if (v != gpu.area_ini) { g_ps2_vram_geracao++; area_velha = 0; }
                 gpu.area_ini = v; break; }
    case 0xe4: { uint32_t v = gpu.pacote[0] & 0xfffffu;
                 if (v != gpu.area_fim) { g_ps2_vram_geracao++; area_velha = 0; }
                 gpu.area_fim = v; break; }
    case 0xe5: { uint32_t v = gpu.pacote[0] & 0x3fffffu;
                 if (v != gpu.offset) g_ps2_vram_geracao++;
                 gpu.offset = v; break; }
    case 0xe6: gpu.mascara = gpu.pacote[0] & 3u; break;
    default: {
        static uint8_t avisado[256];
        if (!avisado[op]) {
            printf("gpu: comando GP0 %02lx nao implementado\n", (unsigned long)op);
            avisado[op] = 1;
        }
        break;
    }
    }
#ifdef PS2_DEBUG_GPU
    if (op >= 0xe3u && op <= 0xe5u)
        printf("gpu dbg: GP0 %02lx valor=%06lx\n", (unsigned long)op,
               (unsigned long)(gpu.pacote[0] & 0xffffffu));
#endif
}

static uint32_t tamanho_pacote(uint32_t op)
{
    if (op == 0x02u || (op >= 0xa0u && op <= 0xdfu)) return 3;
    if (op >= 0x80u && op <= 0x9fu) return 4;
    if (op >= 0x20u && op <= 0x3fu) {
        uint32_t vertices = (op & 8u) ? 4u : 3u;
        return 1u + vertices * (1u + !!(op & 4u)) +
               ((op & 16u) ? vertices - 1u : 0u);
    }
    if (op >= 0x40u && op <= 0x5fu)
        return (op & 16u) ? 4u : 3u;
    if (op >= 0x60u && op <= 0x7fu)
        return 2u + !!(op & 4u) + ((op & 0x18u) == 0u);
    return 1;
}

void ps2_gpu_reset(void)
{
    memset(&gpu, 0, sizeof(gpu));
    memset(vram, 0, sizeof(vram));
    memset(sombra_ok, 0xFF, sizeof(sombra_ok));     /* sombra e GS começam zerados */
    memset(escrita_vb, 0, sizeof(escrita_vb));
    vb_contador = 0;
    area_velha = 0;
    gpu.display_off = 1;
}

PS2_QUENTE unsigned long g_ps2_gpu_palavras;
PS2_QUENTE unsigned long long g_ps2_gpu_ciclos; /* diagnóstico: ciclos do EE desenhando */     /* diagnóstico: palavras GP0 recebidas */

int g_ps2_gpu_pk = 1;   /* V29: caminho direto dos polígonos (0 = antigo) */
static PS2_QUENTE int medindo_bloco;   /* ps2_gpu_gp0_bloco já mede: não somar 2 vezes */

void ps2_gpu_gp0(uint32_t palavra)
{
    g_ps2_gpu_palavras++;
    if (gpu.transferencia == 1) {
        transferencia_escrever((uint16_t)palavra);
        if (gpu.transferencia == 1)
            transferencia_escrever((uint16_t)(palavra >> 16));
        return;
    }
    if (gpu.polilinha) {
        if (!gpu.linha_aguarda_pos &&
            (palavra & 0xf000f000u) == 0x50005000u) {
            gpu.polilinha = 0;
            return;
        }
        if ((gpu.linha_op & 16u) && !gpu.linha_aguarda_pos) {
            gpu.linha_proxima_cor = palavra;
            gpu.linha_aguarda_pos = 1;
            return;
        }
        if (!(gpu.linha_op & 16u)) gpu.linha_proxima_cor = gpu.linha_cor;
        linha(vertice(gpu.linha_pos, gpu.linha_cor, 0),
              vertice(palavra, gpu.linha_proxima_cor, 0), gpu.linha_op);
        gpu.linha_pos = palavra;
        gpu.linha_cor = gpu.linha_proxima_cor;
        gpu.linha_aguarda_pos = 0;
        return;
    }
    if (gpu.recebido == 0)
        gpu.esperado = tamanho_pacote(palavra >> 24);
    gpu.pacote[gpu.recebido++] = palavra;
    if (gpu.recebido == gpu.esperado) {
#if defined(_EE) && !defined(PS2_LIMPO)
        if (medindo_bloco) executar();
        else {
            uint32_t t0, t1;
            __asm__ volatile("mfc0 %0, $9" : "=r"(t0));
            executar();
            __asm__ volatile("mfc0 %0, $9" : "=r"(t1));
            g_ps2_gpu_ciclos += t1 - t0;
        }
#else
        executar();
#endif
        gpu.recebido = 0;
    }
}

/* Vários comandos inteiros de uma vez (nó da lista de DMA): copia direto
 * para o pacote e executa, sem passar palavra por palavra. */
void ps2_gpu_gp0_bloco(const uint32_t *p, unsigned n)
{
    unsigned i = 0;
#if defined(_EE) && !defined(PS2_LIMPO)
    uint32_t t0, t1;
    __asm__ volatile("mfc0 %0, $9" : "=r"(t0));
#endif
    medindo_bloco = 1;
    while (i < n) {
        if (!gpu.transferencia && !gpu.polilinha && gpu.recebido == 0) {
            uint32_t tam = tamanho_pacote(p[i] >> 24);
            if (tam <= n - i) {
#if defined(_EE) && !defined(PS2_DEBUG_GPU)
                {   /* V29: polígono direto do pacote para a fila do GS */
                    uint32_t op = p[i] >> 24;
                    if (op >= 0x20u && op <= 0x3fu && GS_ATIVO && g_ps2_gpu_pk) {
                        const uint32_t *pk = &p[i];
                        uint32_t clut = 0;
                        if (op & 4u) {
                            clut = pk[2] >> 16;
                            gpu.modo = (gpu.modo & ~0x9ffu) | ((pk[(op & 16u) ? 5u : 4u] >> 16) & 0x9ffu);
                        }
                        {
                            GsrEstado e = estado_gs();
                            gsr_poligono_pk(&e, pk, op, clut, sinal11(gpu.offset), sinal11(gpu.offset >> 11));
                        }
                        g_ps2_gpu_palavras += tam;
                        i += tam;
                        continue;
                    }
                }
#endif
                memcpy(gpu.pacote, &p[i], tam * 4u);
                g_ps2_gpu_palavras += tam;
                executar();
                i += tam;
                continue;
            }
        }
        ps2_gpu_gp0(p[i++]);
    }
    medindo_bloco = 0;
#if defined(_EE) && !defined(PS2_LIMPO)
    __asm__ volatile("mfc0 %0, $9" : "=r"(t1));
    g_ps2_gpu_ciclos += (uint32_t)(t1 - t0);
#endif
}

uint32_t ps2_gpu_read(void)
{
    uint32_t valor;
    if (gpu.transferencia != 2) return gpu.leitura;
    valor = transferencia_ler();
    if (gpu.transferencia == 2) valor |= (uint32_t)transferencia_ler() << 16;
    gpu.leitura = valor;
    return valor;
}

void ps2_gpu_gp1(uint32_t palavra)
{
#ifdef PS2_DEBUG_GPU
    if ((palavra >> 24) >= 0x05u && (palavra >> 24) <= 0x08u)
        printf("gpu dbg: GP1 %02lx valor=%06lx\n",
               (unsigned long)(palavra >> 24), (unsigned long)(palavra & 0xffffffu));
#endif
    switch (palavra >> 24) {
    case 0x00:
        /* O reset da GPU não apaga a VRAM. */
        memset(&gpu, 0, sizeof(gpu));
        gpu.display_off = 1;
        g_ps2_vram_geracao++;
        break;
    case 0x01: gpu.recebido = 0; gpu.transferencia = 0; gpu.polilinha = 0; break;
    case 0x03: gpu.display_off = palavra & 1u; break;
    case 0x04: gpu.dma_direction = palavra & 3u; break;
    case 0x05: gpu.display_x = palavra & 1023u; gpu.display_y = (palavra >> 10) & 511u; break;
    case 0x06: gpu.display_h = palavra & 0xffffffu; break;
    case 0x07: gpu.display_v = palavra & 0xfffffu; break;
    case 0x08: gpu.display_mode = palavra & 0xffu; break;
    case 0x10:
        switch (palavra & 15u) {
        case 2: gpu.leitura = gpu.janela; break;
        case 3: gpu.leitura = gpu.area_ini; break;
        case 4: gpu.leitura = gpu.area_fim; break;
        case 5: gpu.leitura = gpu.offset; break;
        case 7: gpu.leitura = 2; break;
        default: break;
        }
        break;
    default: break;
    }
}

/* Campo atual (par/ímpar) do vídeo entrelaçado: alterna a cada VBlank.
 * A VSync() da libetc espera o bit 31 do GPUSTAT mudar no modo 480i. */
static uint32_t campo_impar;

void ps2_gpu_vblank(void)
{
    campo_impar ^= 1u;
    vb_contador++;                      /* V50: carimbo das escritas (envelhecer) */
}

uint32_t ps2_gpu_status(void)
{
    uint32_t modo = gpu.display_mode;
    uint32_t status = (gpu.modo & 0x7ffu) |
        ((gpu.mascara & 3u) << 11) |
        ((modo & 0x40u) << 10) |
        ((modo & 0x80u) << 7) |
        ((modo & 3u) << 17) |
        ((modo & 0x3cu) << 17) |
        (gpu.display_off << 23) |
        0x1c000000u | (gpu.dma_direction << 29);
    if (gpu.dma_direction != 0) status |= 1u << 25;
    /* bit 31: linha par/ímpar. Entrelaçado: campo atual; senão alterna a
     * cada leitura (simula as linhas passando). bit 13: igual ao campo. */
    if (modo & 0x20u) {
        status |= campo_impar << 31;
        status |= campo_impar << 13;
    } else {
        static uint32_t linha;
        linha ^= 1u;
        status |= linha << 31;
    }
    return status;
}

const uint16_t *ps2_gpu_vram(void) { return vram; }

Ps2GpuDisplay ps2_gpu_display(void)
{
    static const unsigned larguras[4] = {256, 320, 512, 640};
    static const unsigned divisoes[4] = {10, 8, 5, 4};
    unsigned modo = gpu.display_mode & 3u;
    unsigned largura = (gpu.display_mode & 0x40u) ? 368u : larguras[modo];
    unsigned div = (gpu.display_mode & 0x40u) ? 7u : divisoes[modo];
    unsigned hi = gpu.display_h & 0xfffu, hf = (gpu.display_h >> 12) & 0xfffu;
    unsigned vi = gpu.display_v & 0x3ffu, vf = (gpu.display_v >> 10) & 0x3ffu;
    unsigned h = vf > vi ? vf - vi : 0;
    unsigned w = hf > hi ? (hf - hi) / div : 0;
    Ps2GpuDisplay d;
    if ((gpu.display_mode & 0x24u) == 0x24u) h *= 2;
    if (w > largura) w = largura;
    if (h > 512) h = 512;
    d.x = gpu.display_x; d.y = gpu.display_y;
    d.largura = w; d.altura = h;
    d.ativo = !gpu.display_off && w && h;
    d.cor24 = !!(gpu.display_mode & 0x10u);
    return d;
}
