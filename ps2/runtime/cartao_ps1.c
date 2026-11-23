/* cartao_ps1.c — ver cartao_ps1.h. */
#include "cartao_ps1.h"

#include <string.h>

uint8_t  g_mc_imagem[MC_TAM] __attribute__((aligned(64)));
int      g_mc_presente;
unsigned g_mc_mudou;

/* Estado de cada entrada do diretório (byte 0 do quadro 1..15) */
enum { E_PRIMEIRO = 0x51, E_MEIO = 0x52, E_ULTIMO = 0x53, E_LIVRE = 0xA0 };

static uint8_t *quadro(uint8_t *img, unsigned q) { return img + q * MC_QUADRO; }
static uint8_t *entrada_dir(unsigned bloco) { return quadro(g_mc_imagem, bloco); }  /* bloco 1..15 */

static void soma_verif(uint8_t *q)
{
    uint8_t x = 0;
    unsigned i;
    for (i = 0; i < 0x7F; i++) x ^= q[i];
    q[0x7F] = x;
}

static void poe32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static void poe16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static int livre(unsigned bloco) { return (entrada_dir(bloco)[0] & 0xF0u) == 0xA0u; }   /* A0..A3 (apagado vale) */

void mc_formatar(uint8_t *img)
{
    unsigned q;
    memset(img, 0, MC_TAM);
    img[0] = 'M'; img[1] = 'C';
    soma_verif(quadro(img, 0));
    for (q = 1; q < 16; q++) {          /* entradas livres */
        uint8_t *e = quadro(img, q);
        poe32(e, E_LIVRE);
        poe16(e + 8, 0xFFFFu);
        soma_verif(e);
    }
    for (q = 16; q < 36; q++) {         /* lista de setores ruins: vazia */
        uint8_t *e = quadro(img, q);
        poe32(e, 0xFFFFFFFFu);
        poe16(e + 8, 0xFFFFu);
        soma_verif(e);
    }
    memcpy(quadro(img, 63), quadro(img, 0), MC_QUADRO);
}

/* Cada arquivo (51h) tem de ser uma cadeia 51h [52h...] 53h (ou só 51h
 * com próximo FFFFh), sem ciclo e sem bloco de outro arquivo; e nenhum 52h
 * ou 53h pode ficar solto. */
int mc_valida(const uint8_t *img)
{
    unsigned b, dono[MC_BLOCOS + 1];
    if (img[0] != 'M' || img[1] != 'C') return 0;
    memset(dono, 0, sizeof(dono));
    for (b = 1; b <= MC_BLOCOS; b++) {
        unsigned atual = b, n = 0;
        if (img[b * MC_QUADRO] != E_PRIMEIRO) continue;
        for (;;) {
            const uint8_t *e = img + atual * MC_QUADRO;
            unsigned prox = le16(e + 8);
            if (dono[atual] || ++n > MC_BLOCOS) return 0;       /* compartilhado ou ciclo */
            if (n > 1 && e[0] != E_MEIO && e[0] != E_ULTIMO) return 0;
            dono[atual] = b;
            if (prox == 0xFFFFu) {
                if (n > 1 && e[0] != E_ULTIMO) return 0;         /* cadeia sem fim */
                break;
            }
            if (prox >= MC_BLOCOS || e[0] == E_ULTIMO) return 0;
            atual = prox + 1u;
        }
    }
    for (b = 1; b <= MC_BLOCOS; b++)                            /* 52h/53h soltos */
        if ((img[b * MC_QUADRO] == E_MEIO || img[b * MC_QUADRO] == E_ULTIMO) && !dono[b]) return 0;
    return 1;
}

/* Bloco inicial (1..15) do arquivo, 0 se não existe. */
static unsigned achar(const char *nome)
{
    unsigned b;
    for (b = 1; b <= MC_BLOCOS; b++) {
        const uint8_t *e = entrada_dir(b);
        if (e[0] == E_PRIMEIRO && strncmp((const char *)e + 0x0A, nome, MC_NOME - 1) == 0) return b;
    }
    return 0;
}

/* n-ésimo bloco (0 = o inicial) da corrente que começa em b; 0 se acabou. */
static unsigned bloco_n(unsigned b, unsigned n)
{
    while (n--) {
        unsigned prox = le16(entrada_dir(b) + 8);
        if (prox >= MC_BLOCOS) return 0;
        b = prox + 1u;
    }
    return b;
}

static unsigned blocos_de(unsigned b)
{
    unsigned n = 1;
    while (n <= MC_BLOCOS) {
        unsigned prox = le16(entrada_dir(b) + 8);
        if (prox >= MC_BLOCOS) break;
        b = prox + 1u; n++;
    }
    return n;
}

static void gravou(void) { g_mc_mudou++; }

static unsigned criar(const char *nome, unsigned nblocos)
{
    unsigned livres[MC_BLOCOS], n = 0, b, i;
    if (nblocos < 1u || nblocos > MC_BLOCOS) return 0;
    for (b = 1; b <= MC_BLOCOS && n < nblocos; b++)
        if (livre(b)) livres[n++] = b;
    if (n < nblocos) return 0;
    for (i = 0; i < nblocos; i++) {
        uint8_t *e = entrada_dir(livres[i]);
        memset(e, 0, MC_QUADRO);
        e[0] = i == 0 ? E_PRIMEIRO : (i + 1 == nblocos ? E_ULTIMO : E_MEIO);
        if (nblocos == 1) e[0] = E_PRIMEIRO;
        if (i == 0) {
            poe32(e + 4, nblocos * MC_BLOCO);
            strncpy((char *)e + 0x0A, nome, MC_NOME - 1);
        }
        poe16(e + 8, i + 1 < nblocos ? livres[i + 1] - 1u : 0xFFFFu);
        soma_verif(e);
        memset(g_mc_imagem + livres[i] * MC_BLOCO, 0, MC_BLOCO);
    }
    gravou();
    return livres[0];
}

static void fecha_de(unsigned inicio);

int mc_apagar(const char *nome)
{
    unsigned b = achar(nome), n, i;
    if (!b) return 0;
    fecha_de(b);                /* descritores dele deixam de valer */
    n = blocos_de(b);
    for (i = 0; i < n; i++) {
        unsigned prox = le16(entrada_dir(b) + 8);
        uint8_t *e = entrada_dir(b);
        e[0] = (uint8_t)(0xA0u | (e[0] & 0x0Fu));     /* apagado: A1/A2/A3 */
        soma_verif(e);
        if (prox >= MC_BLOCOS) break;
        b = prox + 1u;
    }
    gravou();
    return 1;
}

/* ---- arquivos abertos ---- */
static struct { int usado; unsigned inicio; uint32_t pos, tam, modo; } fds[MC_FDS];

int mc_abrir(const char *nome, uint32_t modo)
{
    unsigned b, i;
    if (!g_mc_presente || !nome[0]) return -1;
    for (i = 0; i < MC_FDS && fds[i].usado; i++) {}
    if (i == MC_FDS) return -1;
    b = achar(nome);
    if (modo & MC_FCREAT) {
        if (b) return -1;               /* já existe */
        b = criar(nome, (modo >> 16) ? (modo >> 16) : 1u);
    }
    if (!b) return -1;
    fds[i].usado = 1; fds[i].inicio = b; fds[i].pos = 0;
    fds[i].tam = blocos_de(b) * MC_BLOCO;
    fds[i].modo = modo;
    return (int)i;
}

static int fd_ok(int fd) { return fd >= 0 && fd < MC_FDS && fds[fd].usado; }

/* inicio 0 = todos */
static void fecha_de(unsigned inicio)
{
    int i;
    for (i = 0; i < MC_FDS; i++)
        if (fds[i].usado && (!inicio || fds[i].inicio == inicio)) fds[i].usado = 0;
}

void mc_formatar_cartao(void)
{
    fecha_de(0);
    mc_formatar(g_mc_imagem);
    gravou();
}
int mc_modo(int fd) { return fd_ok(fd) ? (int)fds[fd].modo : -1; }

/* Copia n bytes entre a posição do arquivo e buf (escrever = 1: buf -> cartão). */
static int transferir(int fd, uint8_t *buf, uint32_t n, int escrever)
{
    uint32_t feito = 0;
    if (!fd_ok(fd) || (n % MC_QUADRO) || (fds[fd].pos % MC_QUADRO)) return -1;
    if (fds[fd].pos + n > fds[fd].tam) n = fds[fd].tam - fds[fd].pos;
    while (feito < n) {
        uint32_t pos = fds[fd].pos, off = pos % MC_BLOCO, parte = MC_BLOCO - off;
        unsigned b = bloco_n(fds[fd].inicio, pos / MC_BLOCO);
        uint8_t *dado;
        if (!b) break;
        if (parte > n - feito) parte = n - feito;
        dado = g_mc_imagem + b * MC_BLOCO + off;
        if (escrever) memcpy(dado, buf + feito, parte);
        else memcpy(buf + feito, dado, parte);
        fds[fd].pos += parte; feito += parte;
    }
    if (escrever && feito) gravou();
    return (int)feito;
}

int mc_ler(int fd, uint8_t *dst, uint32_t n) { return transferir(fd, dst, n, 0); }
int mc_escrever(int fd, const uint8_t *src, uint32_t n) { return transferir(fd, (uint8_t *)src, n, 1); }

int mc_posicionar(int fd, int32_t pos, int de)
{
    int64_t nova;
    if (!fd_ok(fd)) return -1;
    nova = (de == 1 ? (int64_t)fds[fd].pos : 0) + pos;
    if (nova < 0 || nova > (int64_t)fds[fd].tam) return -1;
    fds[fd].pos = (uint32_t)nova;
    return (int)nova;
}

int mc_fechar(int fd)
{
    if (!fd_ok(fd)) return -1;
    fds[fd].usado = 0;
    return fd;
}

/* ---- procura ---- */
static char padrao_atual[MC_NOME];
static unsigned procura_em;

static int casa(const char *p, const char *s)
{
    for (;; p++, s++) {
        if (*p == '*') return 1;
        if (!*p) return !*s;
        if (!*s) return 0;
        if (*p != '?' && *p != *s) return 0;
    }
}

static int procura(uint8_t ent[0x28])
{
    for (; procura_em <= MC_BLOCOS; procura_em++) {
        const uint8_t *e = entrada_dir(procura_em);
        char nome[MC_NOME];
        if (e[0] != E_PRIMEIRO) continue;
        memcpy(nome, e + 0x0A, MC_NOME - 1);
        nome[MC_NOME - 1] = 0;
        if (!casa(padrao_atual, nome)) continue;
        memset(ent, 0, 0x28);
        memcpy(ent, nome, 20);
        poe32(ent + 0x14, e[0]);
        poe32(ent + 0x18, blocos_de(procura_em) * MC_BLOCO);
        poe32(ent + 0x20, procura_em);
        procura_em++;
        return 1;
    }
    return 0;
}

int mc_primeiro(const char *padrao, uint8_t ent[0x28])
{
    if (!g_mc_presente) return 0;
    strncpy(padrao_atual, padrao[0] ? padrao : "*", MC_NOME - 1);
    padrao_atual[MC_NOME - 1] = 0;
    procura_em = 1;
    return procura(ent);
}

int mc_proximo(uint8_t ent[0x28]) { return g_mc_presente ? procura(ent) : 0; }

int mc_quadro_ler(unsigned q, uint8_t dst[MC_QUADRO])
{
    if (!g_mc_presente || q >= MC_TAM / MC_QUADRO) return 0;
    memcpy(dst, quadro(g_mc_imagem, q), MC_QUADRO);
    return 1;
}

int mc_quadro_escrever(unsigned q, const uint8_t src[MC_QUADRO])
{
    if (!g_mc_presente || q >= MC_TAM / MC_QUADRO) return 0;
    if (memcmp(quadro(g_mc_imagem, q), src, MC_QUADRO)) {
        memcpy(quadro(g_mc_imagem, q), src, MC_QUADRO);
        gravou();
    }
    return 1;
}
