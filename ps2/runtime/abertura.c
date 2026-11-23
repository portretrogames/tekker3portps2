/*
 * abertura.c — ver abertura.h. Desenho adaptado de formato.c e intro.c do
 * port do Super Mario World para PS2 (smw-ps2, v0.16), do mesmo autor:
 * a vinheta, a transição em círculo e a tela de apoio são as mesmas; o
 * fundo (lá uma cena do jogo tirada da ROM) aqui é desenhado no código, e
 * os textos são os do Tekken. Sem a fonte do jogo: só a 8x8 do PS2SDK.
 */
#include "abertura.h"
#include "abertura_qr.h"
#ifdef _EE
#include "video_gs.h"
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>

#define W ABERTURA_W
#define H ABERTURA_H

/* fonte 8x8 da libdebug do PS2SDK (scr_printf); no teste do PC, a do teste */
extern unsigned char msx[];

static uint32_t *px;

enum {
    OURO = 0xF8D030, BRANCO = 0xFFFFFF, CINZA = 0xB4B4C0, AZUL = 0x9FD0FF, PRETO = 0x000001
};

static void fill(int x, int y, int w, int h, uint32_t rgb)
{
    int i, j;
    for (j = y; j < y + h; j++)
        if ((unsigned)j < H)
            for (i = x; i < x + w; i++)
                if ((unsigned)i < W) px[j * W + i] = rgb;
}

/* Pinta por cima com transparência (a = 0..256). */
static void mistura(int x, int y, int w, int h, uint32_t rgb, int a)
{
    int i, j, s;
    for (j = y; j < y + h; j++)
        if ((unsigned)j < H)
            for (i = x; i < x + w; i++) {
                uint32_t p, out = 0;
                if ((unsigned)i >= W) continue;
                p = px[j * W + i];
                for (s = 0; s < 24; s += 8) {
                    int c = (int)((p >> s) & 255), n = (int)((rgb >> s) & 255);
                    out |= (uint32_t)(c + (n - c) * a / 256) << s;
                }
                px[j * W + i] = out;
            }
}

static void contorno(int x, int y, int w, int h, int b, uint32_t rgb)
{
    fill(x, y, w, b, rgb); fill(x, y + h - b, w, b, rgb);
    fill(x, y, b, h, rgb); fill(x + w - b, y, b, h, rgb);
}

/* ---- texto (fonte 8x8 do PS2SDK; cada letra ocupa as colunas com tinta) ---- */

typedef struct { uint8_t l[8]; uint8_t esq, larg; } Glifo;

/* Um caractere do texto em UTF-8 como código Latin-1; fora dele, '?'. */
static uint8_t proximo(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    int c = *s++;
    if (c >= 0x80) {
        if ((c == 0xC2 || c == 0xC3) && (*s & 0xC0) == 0x80) {
            c = (c & 3) << 6 | (*s++ & 0x3F);
        } else {
            c = '?';
            while ((*s & 0xC0) == 0x80) s++;
        }
    }
    *p = (const char *)s;
    return (uint8_t)c;
}

static Glifo glifo(uint8_t c)
{
    /* os acentos do português estão na metade de cima da fonte MSX */
    static const unsigned char acentos[][2] = {
        {0xE1, 0xA0}, {0xE0, 0x85}, {0xE2, 0x83}, {0xE3, 0xB1}, {0xE7, 0x87}, {0xE9, 0x82},
        {0xEA, 0x88}, {0xED, 0xA1}, {0xF3, 0xA2}, {0xF4, 0x93}, {0xF5, 0xB5}, {0xFA, 0xA3},
        {0xFC, 0x81}, {0xC7, 0x80}, {0xC9, 0x90}, {0xC3, 0xB0}, {0xD5, 0xB4}};
    Glifo g;
    uint8_t tudo = 0;
    int m = c, r, a, b;
    size_t i;
    if (c >= 0x80) {
        m = '?';
        for (i = 0; i < sizeof(acentos) / sizeof(acentos[0]); i++)
            if (acentos[i][0] == c) m = acentos[i][1];
    }
    for (r = 0; r < 8; r++) { g.l[r] = msx[m * 8 + r] & 0xFC; tudo |= g.l[r]; }
    if (!tudo) { g.esq = 0; g.larg = 3; return g; }      /* espaço */
    a = 0; b = 7;
    while (!(tudo & (0x80 >> a))) a++;
    while (!(tudo & (0x80 >> b))) b--;
    g.esq = (uint8_t)a; g.larg = (uint8_t)(b - a + 1);
    return g;
}

static int largura(const char *s, int escala)
{
    int n = 0;
    while (*s) n += glifo(proximo(&s)).larg + 1;
    return n ? (n - 1) * escala : 0;
}

static void texto(int x, int y, const char *s, uint32_t rgb, int escala)
{
    while (*s) {
        Glifo g = glifo(proximo(&s));
        int r, c;
        for (r = 0; r < 8; r++)
            for (c = 0; c < g.larg; c++)
                if (g.l[r] & (0x80 >> (g.esq + c))) fill(x + c * escala, y + r * escala, escala, escala, rgb);
        x += (g.larg + 1) * escala;
    }
}

/* Centrado na faixa [x0, x0 + w). */
static void texto_em(int x0, int w, int y, const char *s, uint32_t rgb)
{
    texto(x0 + (w - largura(s, 1)) / 2, y, s, rgb, 1);
}

/* Centrado na tela, com contorno preto: para ler por cima do fundo. */
static void texto_contorno(int y, const char *s, uint32_t rgb, int escala)
{
    int x = (W - largura(s, escala)) / 2, dx, dy;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++)
            if (dx || dy) texto(x + dx, y + dy, s, PRETO, escala);
    texto(x, y, s, rgb, escala);
}

/* ---- fundo (desenhado aqui): degradê escuro, do azul-noite em cima ao
 * vermelho-escuro embaixo, com faixas diagonais claras e um piso em
 * perspectiva. escuro/256 escurece tudo. ---- */
static uint32_t *fundo_cache;      /* PS2: o fundo pronto (ver abertura_cache) */
static int fundo_cache_escuro = -1;

static void fundo_desenha(int escuro)
{
    int x, y;
    for (y = 0; y < H; y++) {
        int t = y * 256 / H;
        uint32_t r = (uint32_t)(10 + 70 * t / 256), g = (uint32_t)(10 + 6 * t / 256), b = (uint32_t)(30 - 20 * t / 256);
        for (x = 0; x < W; x++) {
            uint32_t rr = r, gg = g, bb = b;
            int d = (x + y * 2) % 96;                   /* faixas diagonais */
            if (d < 10) { rr += 24u - (uint32_t)d * 2u; gg += 6u; }
            if (y >= 160) {                             /* piso: linhas que se abrem */
                int k = y - 160, cx = x - W / 2;
                if (cx < 0) cx = -cx;
                if ((cx * 16 / (k + 8)) % 8 == 0 || (k > 0 && (k * k / 40) % 6 == 0)) { rr += 30u; gg += 12u; bb += 4u; }
            }
            if (rr > 255u) rr = 255u;
            if (gg > 255u) gg = 255u;
            px[y * W + x] = rr << 16 | gg << 8 | bb;
        }
    }
    if (escuro > 0) mistura(0, 0, W, H, 0x000000, escuro);
}

/* O fundo é o mesmo em todo quadro de uma tela: com cache, desenha uma vez
 * por escurecimento e depois só copia. */
static void fundo(int escuro)
{
    if (!fundo_cache) { fundo_desenha(escuro); return; }
    if (fundo_cache_escuro != escuro) {
        uint32_t *alvo = px;
        px = fundo_cache;
        fundo_desenha(escuro);
        px = alvo;
        fundo_cache_escuro = escuro;
    }
    memcpy(px, fundo_cache, sizeof(uint32_t) * W * H);
}

void abertura_cache(uint32_t *buf)  { fundo_cache = buf; fundo_cache_escuro = -1; }

/* Caixa preta de borda dupla, um pouco transparente. */
static void caixa_a(int x, int y, int w, int h, int opaca)
{
    mistura(x, y, w, h, 0x000000, opaca);
    contorno(x, y, w, h, 2, 0xF0F0F0);
    contorno(x + 3, y + 3, w - 6, h - 6, 1, 0x808080);
}

/* ---- vinheta da Retrogames ---- */

enum { VX = 18, VY = 16, VW = W - 36, VH = 192, V_PARTICULAS = 160, V_CY = 76 };
static float v_angulo[V_PARTICULAS], v_raio[V_PARTICULAS];
static int v_pronta;

/* Soma luz (brilho aditivo), só dentro da caixa. */
static void soma(int x, int y, unsigned r, unsigned g, unsigned b)
{
    uint32_t p, nr, ng, nb;
    if (x < VX + 3 || x >= VX + VW - 3 || y < VY + 3 || y >= VY + VH - 3) return;
    p = px[y * W + x];
    nr = ((p >> 16) & 255) + r; ng = ((p >> 8) & 255) + g; nb = (p & 255) + b;
    px[y * W + x] = (nr > 255 ? 255 : nr) << 16 | (ng > 255 ? 255 : ng) << 8 | (nb > 255 ? 255 : nb);
}

static void ponto_brilho(int x, int y, unsigned r, unsigned g, unsigned b)
{
    soma(x, y, r, g, b);
    soma(x + 1, y, r / 3, g / 3, b / 3); soma(x - 1, y, r / 3, g / 3, b / 3);
    soma(x, y + 1, r / 3, g / 3, b / 3); soma(x, y - 1, r / 3, g / 3, b / 3);
}

static void linha_brilho(float x0, float y0, float x1, float y1, unsigned r, unsigned g, unsigned b)
{
    float dx = x1 - x0, dy = y1 - y0;
    int passos = (int)sqrtf(dx * dx + dy * dy) + 1, i;
    for (i = 0; i <= passos; i++) {
        float t = (float)i / passos;
        ponto_brilho((int)(x0 + dx * t), (int)(y0 + dy * t), r, g, b);
    }
}

static void cubo(float cx, float cy, float lado, float ay, float ax, int nivel)
{
    static const int arestas[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4},
                                       {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    float sx[8], sy[8];
    const float cy_ = cosf(ay), sy_ = sinf(ay), cx_ = cosf(ax), sx_ = sinf(ax);
    int i;
    for (i = 0; i < 8; i++) {
        float x = (i & 1) ? 1.f : -1.f, y = (i & 2) ? 1.f : -1.f, z = (i & 4) ? 1.f : -1.f;
        float x1 = x * cy_ + z * sy_, z1 = -x * sy_ + z * cy_;
        float y1 = y * cx_ - z1 * sx_, z2 = y * sx_ + z1 * cx_;
        float k = 4.0f / (z2 + 6.0f);
        sx[i] = cx + x1 * lado * k * 0.8f; sy[i] = cy + y1 * lado * k * 0.9f;
    }
    for (i = 0; i < 12; i++)
        linha_brilho(sx[arestas[i][0]], sy[arestas[i][0]], sx[arestas[i][1]], sy[arestas[i][1]],
                     (unsigned)(255 * nivel >> 8), (unsigned)(190 * nivel >> 8), (unsigned)(60 * nivel >> 8));
}

static uint32_t nivel_cor(uint32_t rgb, int nivel)     /* nivel 0..256 */
{
    if (nivel <= 0) return 0;
    if (nivel >= 256) return rgb;
    return ((((rgb >> 16) & 255) * (uint32_t)nivel >> 8) << 16) |
           ((((rgb >> 8) & 255) * (uint32_t)nivel >> 8) << 8) | ((rgb & 255) * (uint32_t)nivel >> 8);
}

/* Um brilho que corre pela faixa [y0, y1) clareando só as letras. */
static void varre(int y0, int y1, int em)
{
    int x, y;
    for (y = y0; y < y1; y++)
        for (x = em - 15; x < em + 15; x++) {
            int k;
            if ((unsigned)x >= W || (unsigned)y >= H || ((px[y * W + x] >> 16) & 255) < 100) continue;
            k = 15 - (x > em ? x - em : em - x);
            soma(x, y, (unsigned)k * 10, (unsigned)k * 10, (unsigned)k * 8);
        }
}

void abertura_vinheta(uint32_t *destino, int f)
{
    const int trava = ABERTURA_VINHETA_TRAVA;
    const float t = (float)f;
    const char *nome = "RETROGAMES";
    uint32_t cor;
    int i;
    px = destino;
    if (!v_pronta) {    /* sempre as mesmas partículas */
        unsigned semente = 0x1234567u;
        for (i = 0; i < V_PARTICULAS; i++) {
            semente = semente * 1103515245u + 12345u;
            v_angulo[i] = (float)((semente >> 8) & 0xFFFF) / 65535.0f * 6.2831853f;
            semente = semente * 1103515245u + 12345u;
            v_raio[i] = 104 + (float)((semente >> 8) & 0xFFFF) / 65535.0f * 104;
        }
        v_pronta = 1;
    }
    fundo(50);
    caixa_a(VX, VY, VW, VH, 228);
    if (f < trava) {    /* partículas em espiral até o centro */
        const float k = 1.0f - (float)f / trava, gira = t * 0.04f + (1 - k) * 3.0f;
        for (i = 0; i < V_PARTICULAS; i++) {
            float r = v_raio[i] * k * k, a = v_angulo[i] + gira;
            ponto_brilho(W / 2 + (int)(r * cosf(a) * 0.8f), V_CY + (int)(r * sinf(a) * 0.75f), 160, 120, 50);
        }
    } else if (f < trava + 40) {    /* estouro: saem de novo, sumindo */
        const unsigned some = (unsigned)(200 - (f - trava) * 5);
        for (i = 0; i < V_PARTICULAS; i++) {
            float e = (float)(f - trava) * 3.0f * (0.5f + v_raio[i] / 208.0f);
            ponto_brilho(W / 2 + (int)(e * cosf(v_angulo[i]) * 0.8f), V_CY + (int)(e * sinf(v_angulo[i]) * 0.75f),
                         some, some * 3 / 4, some / 3);
        }
    }
    if (f >= trava - 30) {
        int cresce = f < trava ? (f - (trava - 30)) * 256 / 30 : 256;
        cubo(W / 2, V_CY, 35.0f * (0.4f + 0.6f * cresce / 256.0f), t * 0.035f, 0.5f + 0.25f * sinf(t * 0.02f),
             f < trava ? cresce / 2 : 256);
    }
    if (f >= trava && f < trava + 12) {     /* clarão, dentro da caixa */
        unsigned k = (unsigned)(trava + 12 - f) * 20;
        int x, y;
        for (y = VY + 3; y < VY + VH - 3; y++)
            for (x = VX + 3; x < VX + VW - 3; x++) soma(x, y, k, k, k);
    }
    /* os textos aparecem aos poucos (cor 0 = ainda não aparece) */
    cor = f > trava + 20 ? nivel_cor(0xF2C14E, (f - (trava + 20)) * 8) : 0;
    if (cor) texto((W - largura(nome, 3)) / 2, 132, nome, cor, 3);
    if (f >= trava + 40 && f < trava + 130) varre(128, 158, 20 + (f - (trava + 40)) * 3);
    cor = f > trava + 60 ? nivel_cor(0xC8D0F0, (f - (trava + 60)) * 8) : 0;
    if (cor) texto_em(0, W, 166, "Patos de Minas - MG", cor);
    cor = f > trava + 100 ? nivel_cor(0x9098B8, (f - (trava + 100)) * 6) : 0;
    if (cor) texto_em(0, W, 186, "apresenta", cor);
}

/* ---- transição em círculo ---- */

void abertura_iris(uint32_t *destino, int r)
{
    const float aspecto = 1.1667f;  /* o pixel de 256 x 224 numa TV 4:3 é mais largo que alto */
    float r2;
    int y;
    if (r >= ABERTURA_IRIS_MAX) return;
    if (r <= 0) { memset(destino, 0, sizeof(uint32_t) * W * H); return; }
    r2 = (float)r * (float)r;
    for (y = 0; y < H; y++) {
        float dy = (float)y + 0.5f - H / 2.0f;
        int x0 = W, x1 = W;     /* fora do círculo: a linha toda */
        if (dy * dy < r2) {
            float meia = sqrtf(r2 - dy * dy) / aspecto;
            if (meia >= W / 2.0f) continue;     /* a linha toda dentro */
            x0 = (int)(W / 2.0f - meia + 0.5f); x1 = (int)(W / 2.0f + meia + 0.5f);
        }
        if (x0 > 0) memset(destino + y * W, 0, sizeof(uint32_t) * (size_t)x0);
        if (x1 < W) memset(destino + y * W + x1, 0, sizeof(uint32_t) * (size_t)(W - x1));
    }
}

int abertura_iris_raio(int q, int abrindo)
{
    float t;
    if (q <= 0) return abrindo ? 0 : ABERTURA_IRIS_MAX;
    if (q >= ABERTURA_IRIS_QUADROS) return abrindo ? ABERTURA_IRIS_MAX : 0;
    t = (float)q / ABERTURA_IRIS_QUADROS;
    t = t * t * (3 - 2 * t);    /* começa e termina devagar */
    return (int)lroundf(ABERTURA_IRIS_MAX * (abrindo ? t : 1 - t));
}

/* ---- título e "apoie o projeto" ---- */

void abertura_titulo(uint32_t *destino, const char *versao)
{
    const char *sub = "Port nativo para PlayStation 2";
    char linha[48];
    int w1, w2, w;
    px = destino;
    fundo(40);
    texto_contorno(52, "TEKKEN 3", OURO, 4);
    snprintf(linha, sizeof(linha), "versão %s - projeto de fã, gratuito", versao);
    w1 = largura(sub, 1); w2 = largura(linha, 1); w = w1 > w2 ? w1 : w2;
    mistura((W - w) / 2 - 8, 102, w + 16, 30, 0x000000, 200);
    texto_em(0, W, 106, sub, BRANCO);
    texto_em(0, W, 120, linha, CINZA);
    mistura(0, 150, W, 34, 0x000000, 170);
    texto_em(0, W, 154, "1 ou 2 jogadores", AZUL);
    texto_em(0, W, 168, "direcional ou analógico", AZUL);
}

void abertura_apoio(uint32_t *destino, int frame, int pode_seguir)
{
    static const struct { const char *t; uint32_t c; } linhas[] = {
        {"Este port é", BRANCO}, {"gratuito e sempre", BRANCO}, {"vai ser.", BRANCO}, {"", 0},
        {"Se ele te divertiu,", AZUL}, {"ajude a fazer os", AZUL}, {"próximos ports", AZUL},
        {"para PS2.", AZUL}, {"", 0}, {"", 0},
        {"Pix: aponte a", OURO}, {"câmera do celular", OURO}, {"para o QR.", OURO}};
    const int n = ABERTURA_QR_N, m = 2, q = 4, lado = (n + 2 * q) * m, ox = 8, oy = 32;
    const int bx = ox + lado + 6, bw = W - 6 - bx;
    int x, y, i;
    px = destino;
    fundo(0);
    texto_contorno(8, "APOIE O PROJETO", OURO, 2);
    /* QR: 2 pixels por módulo, borda branca de 4 módulos, preto no branco */
    fill(ox - 2, oy - 2, lado + 4, lado + 4, 0x000000);
    fill(ox, oy, lado, lado, 0xFFFFFF);
    for (y = 0; y < n; y++)
        for (x = 0; x < n; x++)
            if (abertura_qr[y][x] == '1') fill(ox + (q + x) * m, oy + (q + y) * m, m, m, 0x000000);
    caixa_a(bx, oy - 2, bw, 142, 245);
    for (i = 0; i < (int)(sizeof(linhas) / sizeof(linhas[0])); i++)
        if (linhas[i].t[0]) texto(bx + 7, oy + 5 + i * 10, linhas[i].t, linhas[i].c, 1);
    mistura(0, 174, W, H - 174, 0x000000, 210);
    texto_em(0, W, 177, "Do exterior: ko-fi.com/guilhermesoti", AZUL);
    texto_em(0, W, 188, "portretrogames.github.io", AZUL);
    if (pode_seguir && (frame / 30) % 2 == 0) texto_em(0, W, 205, "Pressione X ou START para jogar", BRANCO);
    else if (!pode_seguir) texto_em(0, W, 205, "Retrogames Patos de Minas", CINZA);
}

/* ---- no PS2: o laço das telas, o som e os controles ---- */
#ifdef _EE
#include <malloc.h>
#include <stdlib.h>
#include "kit/controle.h"
#include "kit/diagnostico.h"
#include "kit/som.h"


#define FREQ 48000          /* o que main.c pediu ao kit_som */
#define SOM_QUADROS (FREQ * 9 / 2)
static int16_t *som;
static unsigned som_em;

/* Um "swell" grave que sobe e um acorde em arpejo (o mesmo do Mario). */
static void sintetiza(void)
{
    static const float notas[] = {329.63f, 493.88f, 659.25f, 830.61f, 987.77f, 1318.5f};
    const int carrilhao = FREQ * 3 / 2;
    float sw_c = 1, sw_s = 0;
    int i, n;
    memset(som, 0, SOM_QUADROS * 4);
    for (i = 0; i < carrilhao + FREQ / 2; i++) {   /* swell: 55 -> 110 Hz em 1,5 s */
        float t = (float)i / FREQ, f = 55.0f * (1.0f + t / 1.5f), a = 2 * 3.14159265f * f / FREQ;
        float c = sw_c * cosf(a) - sw_s * sinf(a), s = sw_s * cosf(a) + sw_c * sinf(a);
        float env = t < 1.2f ? t / 1.2f : (t < 1.5f ? 1.0f : 1.0f - (t - 1.5f) * 2.0f);
        int16_t v;
        sw_c = c; sw_s = s;
        if (env < 0) env = 0;
        v = (int16_t)(s * env * 0.22f * 32767);
        som[i * 2] = som[i * 2 + 1] = v;
    }
    for (n = 0; n < 6; n++) {
        int inicio = carrilhao + n * FREQ * 7 / 100;
        float a = 2 * 3.14159265f * notas[n] / FREQ, ca = cosf(a), sa = sinf(a);
        float a2 = 2 * a, ca2 = cosf(a2), sa2 = sinf(a2);
        float c = 1, s = 0, c2 = 1, s2 = 0, env = 0.16f, decai = expf(-2.4f / FREQ);
        float pan = 0.25f + 0.1f * n;              /* da esquerda para a direita */
        for (i = inicio; i < SOM_QUADROS; i++) {
            float nc = c * ca - s * sa, ns = s * ca + c * sa, nc2 = c2 * ca2 - s2 * sa2, ns2 = s2 * ca2 + c2 * sa2;
            float ataque = (float)(i - inicio) / (FREQ / 200);
            float v, l, r;
            c = nc; s = ns; c2 = nc2; s2 = ns2;
            v = (s + 0.3f * s2) * env * (ataque < 1 ? ataque : 1);
            env *= decai;
            l = som[i * 2] / 32767.0f + v * (1 - pan); r = som[i * 2 + 1] / 32767.0f + v * pan;
            if (l > 1) l = 1;
            if (l < -1) l = -1;
            if (r > 1) r = 1;
            if (r < -1) r = -1;
            som[i * 2] = (int16_t)(l * 32767); som[i * 2 + 1] = (int16_t)(r * 32767);
        }
    }
    for (i = SOM_QUADROS - FREQ / 4; i < SOM_QUADROS; i++) {   /* sem estalo no fim */
        float f = (float)(SOM_QUADROS - i) / (FREQ / 4);
        som[i * 2] = (int16_t)(som[i * 2] * f); som[i * 2 + 1] = (int16_t)(som[i * 2 + 1] * f);
    }
}

/* Manda o que couber do acorde; depois dele, silêncio (o IOP repetiria o
 * que ficou no buffer). */
static void alimenta_som(void)
{
    if (!kit_som_pronto()) return;
    kit_som_livre();
    if (som && som_em < SOM_QUADROS) som_em += kit_som_enviar(som + som_em * 2, SOM_QUADROS - som_em);
    kit_som_completar(FREQ / 10);
}

/* Botões que acabaram de ser apertados (soltos antes), de cada controle. */
static unsigned apertou_agora(void)
{
    static unsigned segurando[2] = { 0xFFFFu, 0xFFFFu };  /* começa "segurando" tudo */
    kit_controle_estado e;
    unsigned novos = 0;
    int p;
    for (p = 0; p < 2; p++) {
        unsigned agora = 0;
        kit_controle_ler(p, &e);
        if (e.conectado) agora = 0xFFFFu ^ e.botoes;
        novos |= agora & ~segurando[p];
        segurando[p] = agora;
    }
    return novos;
}

void ps2_abertura(const char *versao)
{
    /* A: a vinheta abre; B: o título; C: a tela de apoio. */
    enum { A = 20, B = 330, C = 480, IRIS = ABERTURA_IRIS_QUADROS };
    int saindo = -1, sai_em = 0, pula = 0, quadro;
    /* tempo na tela de apoio: soma das diferenças do relógio de quadro em
     * quadro (o COUNT de 32 bits dá a volta a cada ~14,5 s) */
    int na_apoio = 0;
    uint32_t antes = kit_relogio();
    uint64_t apoio_ciclos = 0;
    uint32_t *quadro_px = memalign(64, sizeof(uint32_t) * W * H);
    uint32_t *cache = memalign(64, sizeof(uint32_t) * W * H);

    if (!quadro_px) { free(cache); return; }    /* sem memória: direto ao jogo */
    abertura_cache(cache);
    som = kit_som_pronto() ? malloc(SOM_QUADROS * 4) : NULL;
    som_em = 0;
    if (som) sintetiza();
    for (quadro = 0;; quadro++) {
        int f = quadro + pula, r = ABERTURA_IRIS_MAX;
        unsigned apertou = apertou_agora(), seg;
        if (f < C && (apertou & KIT_BOTAO_START)) { pula += C - f; f = C; }
        {
            uint32_t agora = kit_relogio();
            if (na_apoio) apoio_ciclos += (uint32_t)(agora - antes);
            antes = agora;
        }
        if (f >= C) na_apoio = 1;
        /* na tela de apoio: botão vale depois de 1 s; sozinha em 20 s */
        seg = (unsigned)(apoio_ciclos / ((uint64_t)KIT_CICLOS_POR_MS * 1000u));
        if (na_apoio && seg >= 1u && saindo < 0 && (apertou & (KIT_BOTAO_START | KIT_BOTAO_X))) saindo = 0;
        if (na_apoio && seg >= 20u && saindo < 0) saindo = 0;
        if (f >= A && f < C) alimenta_som();
        else if (kit_som_pronto()) { kit_som_livre(); kit_som_completar(FREQ / 10); }
        if (f < A) {
            r = 0;
        } else if (f < B) {
            abertura_vinheta(quadro_px, f - A);
            if (f < A + IRIS) r = abertura_iris_raio(f - A, 1);
            else if (f >= B - IRIS) r = abertura_iris_raio(f - (B - IRIS), 0);
        } else if (f < C) {
            abertura_titulo(quadro_px, versao);
            if (f < B + IRIS) r = abertura_iris_raio(f - B, 1);
            else if (f >= C - IRIS) r = abertura_iris_raio(f - (C - IRIS), 0);
        } else {
            abertura_apoio(quadro_px, f - C, seg >= 1u);
            if (f < C + IRIS) r = abertura_iris_raio(f - C, 1);
        }
        if (saindo == 0) { sai_em = f; saindo = 1; }
        if (saindo > 0) {
            if (f - sai_em > IRIS) break;
            r = abertura_iris_raio(f - sai_em, 0);
        }
        abertura_iris(quadro_px, r);
        ps2_video_mostrar_rgb(quadro_px, W, H);
    }
    memset(quadro_px, 0, sizeof(uint32_t) * W * H);
    ps2_video_mostrar_rgb(quadro_px, W, H);
    abertura_cache(NULL);
    free(cache);
    free(quadro_px);
    free(som);
    som = NULL;
}
#endif
