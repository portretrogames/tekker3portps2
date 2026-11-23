/* kit/src/cartao.c — ver kit/cartao.h. Base: o files.c do port SMW PS2
 * (abrir/gravar/fechar pela libmc, como os jogos comerciais). */
#include "kit/cartao.h"

#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libmc.h>

/* flags do open do MCSERV (não os do newlib) e origem do mcSeek */
enum { O_LER = 0x0001, O_ESCREVER_CRIAR = 0x0002 | 0x0200 };
enum { DO_INICIO = 0, DO_FIM = 2 };
enum { PEDACO = 8192 };

static int pronto;

int kit_cartao_iniciar(void)
{
    int r;
    if (pronto) return 1;
    r = SifLoadModule("rom0:MCMAN", 0, NULL);
    if (r < 0) { printf("cartao: MCMAN falhou (%d)\n", r); return 0; }
    r = SifLoadModule("rom0:MCSERV", 0, NULL);
    if (r < 0) { printf("cartao: MCSERV falhou (%d)\n", r); return 0; }
    r = mcInit(MC_TYPE_MC);
    if (r < 0) { printf("cartao: mcInit falhou (%d)\n", r); return 0; }
    pronto = 1;
    return 1;
}

static int espera(void)
{
    int cmd = 0, r = -1;
    if (mcSync(MC_WAIT, &cmd, &r) < 0) return -1;
    return r;
}

/* ---- gravação em etapas (estado usado também para recusar leituras) ---- */
enum { PARADO, PASTA, APAGA_TMP, ABRIR, GRAVAR, FECHAR, APAGA_ANTIGO, LIMPA_TMP, FECHAR_ERRO };
static struct {
    int etapa, etapa_falha, porta, n, i, fd, feito, esperando, resultado, erro;
    int final;                  /* 0: gravando ~NOME; 1: NOME */
    int recusas;                /* mcClose recusado seguidas */
    char pasta[32], caminho[72], tmp[72];
    char nomes[4][32];
    kit_cartao_arquivo arqs[4];
} g;

int kit_cartao_tem(int porta)
{
    int tipo = 0, livre = 0, formatado = 0, r;
    if (!pronto || g.etapa != PARADO) return 0;     /* a libmc atende um pedido por vez */
    if (mcGetInfo(porta, 0, &tipo, &livre, &formatado) != 0) return 0;
    r = espera();
    /* 0 = o mesmo cartão, -1 = trocado (vale), -2 = não formatado */
    printf("cartao: porta %d: tipo %d, livre %d KB, formatado %d (r %d)\n", porta + 1, tipo, livre, formatado, r);
    return (r == 0 || r == -1) && tipo == MC_TYPE_PS2 && formatado;
}

static uint8_t buf_es[PEDACO] __attribute__((aligned(64)));

int kit_cartao_ler(int porta, const char *caminho, void *destino, int cap)
{
    int fd, tam, lido = 0;
    if (!pronto || g.etapa != PARADO || !caminho || cap < 0 || (!destino && cap > 0)) return -1;
    if (mcOpen(porta, 0, caminho, O_LER) != 0) return -1;
    fd = espera();
    if (fd < 0) return fd;
    tam = -1;
    if (mcSeek(fd, 0, DO_FIM) == 0) tam = espera();
    if (tam >= 0 && mcSeek(fd, 0, DO_INICIO) == 0 && espera() == 0) {
        int quer = tam < cap ? tam : cap;
        while (lido < quer) {
            int parte = quer - lido < PEDACO ? quer - lido : PEDACO, r = -1;
            if (mcRead(fd, buf_es, parte) == 0) r = espera();
            if (r <= 0) { if (r < 0) lido = r; break; }
            memcpy((uint8_t *)destino + lido, buf_es, (size_t)r);
            lido += r;
        }
    } else {
        lido = tam < 0 ? tam : -1;
    }
    if (mcClose(fd) == 0) espera();
    return lido;
}

int kit_cartao_gravar(int porta, const char *pasta, const kit_cartao_arquivo *arqs, int n)
{
    int i;
    if (!pronto || g.etapa != PARADO || !pasta || !arqs || n < 1 || n > 4) return 0;
    while (*pasta == '/') pasta++;
    if (!*pasta || strchr(pasta, '/') || strlen(pasta) >= sizeof(g.pasta)) return 0;
    for (i = 0; i < n; i++)
        if (!arqs[i].nome || !arqs[i].nome[0] || arqs[i].nome[0] == '~' || strchr(arqs[i].nome, '/') ||
            strlen(arqs[i].nome) + 1 >= sizeof(g.nomes[0]) || !arqs[i].dados || arqs[i].tam <= 0) return 0;
    g.porta = porta; g.n = n; g.i = 0; g.fd = -1; g.feito = 0; g.esperando = 0; g.erro = 0;
    g.recusas = 0; g.etapa_falha = 0;
    strcpy(g.pasta, pasta);
    for (i = 0; i < n; i++) {           /* o nome é copiado; os dados ficam com quem chamou */
        g.arqs[i] = arqs[i];
        strcpy(g.nomes[i], arqs[i].nome);
        g.arqs[i].nome = g.nomes[i];
    }
    g.etapa = PASTA;
    return 1;
}

/* Começo do arquivo g.i: caminhos e se passa por ~NOME. */
static void comeca_arquivo(void)
{
    snprintf(g.caminho, sizeof(g.caminho), "/%s/%s", g.pasta, g.arqs[g.i].nome);
    snprintf(g.tmp, sizeof(g.tmp), "/%s/~%s", g.pasta, g.arqs[g.i].nome);
    g.final = g.arqs[g.i].direto;
}

int kit_cartao_ocupado(void) { return g.etapa != PARADO; }
int kit_cartao_resultado(void) { return g.resultado; }

static void termina(int r)
{
    if (g.etapa != FECHAR_ERRO) g.etapa_falha = g.etapa;    /* em FECHAR_ERRO já veio de falha() */
    g.etapa = PARADO;
    g.fd = -1;
    g.resultado = r;
    if (r < 0) printf("cartao: gravacao falhou (%d) na etapa %d de %s\n", r, g.etapa_falha,
                      g.etapa_falha == PASTA ? g.pasta : (g.final ? g.caminho : g.tmp));
}

/* Erro com o arquivo aberto: fecha (também em etapas) e só então termina. */
static void falha(int r)
{
    g.etapa_falha = g.etapa;
    if (g.fd >= 0) { g.erro = r; g.etapa = FECHAR_ERRO; g.recusas = 0; }
    else termina(r);
}

static void proximo_arquivo(void)
{
    if (++g.i == g.n) { termina(1); return; }
    comeca_arquivo();
    g.etapa = g.final ? APAGA_ANTIGO : APAGA_TMP;
}

void kit_cartao_passo(void)
{
    int cmd = 0, r = -1, s;
    if (g.etapa == PARADO) return;
    if (g.esperando) {
        s = mcSync(MC_NOWAIT, &cmd, &r);
        if (s == 0) return;                         /* ainda trabalhando */
        g.esperando = 0;
        if (s < 0) { printf("cartao: mcSync %d na etapa %d\n", s, g.etapa); r = -1; }   /* nada pendente: falha */
        switch (g.etapa) {
        case PASTA:                 /* -4: já existe */
            if (r < 0 && r != -4) { termina(r); return; }
            comeca_arquivo();
            g.etapa = g.final ? APAGA_ANTIGO : APAGA_TMP;
            break;
        case APAGA_TMP:             /* -4: não havia */
            if (r < 0 && r != -4) { termina(r); return; }
            g.etapa = ABRIR;
            break;
        case ABRIR:
            if (r < 0) { termina(r); return; }
            g.fd = r; g.feito = 0; g.recusas = 0; g.etapa = GRAVAR;
            break;
        case GRAVAR:
            if (r <= 0) { falha(r < 0 ? r : -1); return; }
            g.feito += r;
            if (g.feito >= g.arqs[g.i].tam) g.etapa = FECHAR;
            break;
        case FECHAR:
            g.fd = -1;
            if (r < 0) { termina(r); return; }
            if (!g.final) { g.etapa = APAGA_ANTIGO; break; }
            g.etapa = LIMPA_TMP;    /* NOME inteiro: ~NOME não é mais preciso */
            break;
        case APAGA_ANTIGO:
            if (r < 0 && r != -4) { termina(r); return; }
            g.final = 1;
            g.etapa = ABRIR;
            break;
        case LIMPA_TMP:             /* falhar aqui não estraga nada: NOME está inteiro */
            proximo_arquivo();
            if (g.etapa == PARADO) return;
            break;
        case FECHAR_ERRO:
            g.fd = -1;
            termina(g.erro);
            return;
        }
    }
    if (g.etapa == PARADO) return;
    /* cada arquivo vai primeiro inteiro para ~NOME; depois NOME é apagado e
     * gravado de novo e, por fim, ~NOME apagado. Interrompida em ~NOME: NOME
     * (antigo) está inteiro; depois: NOME falta ou ficou curto e ~NOME está
     * inteiro. (O rom0:MCSERV não tem renomear: mcRename devolve -1.) */
    switch (g.etapa) {              /* manda a próxima etapa */
    case PASTA: {
        char p[40];
        snprintf(p, sizeof(p), "/%s", g.pasta);
        g.esperando = mcMkDir(g.porta, 0, p) == 0;
        break; }
    case APAGA_TMP:
        g.esperando = mcDelete(g.porta, 0, g.tmp) == 0;
        break;
    case ABRIR:
        g.esperando = mcOpen(g.porta, 0, g.final ? g.caminho : g.tmp, O_ESCREVER_CRIAR) == 0;
        break;
    case GRAVAR: {
        int parte = g.arqs[g.i].tam - g.feito;
        if (parte > PEDACO) parte = PEDACO;
        /* pelo buffer alinhado: quem chama não precisa alinhar */
        memcpy(buf_es, (const uint8_t *)g.arqs[g.i].dados + g.feito, (size_t)parte);
        g.esperando = mcWrite(g.fd, buf_es, parte) == 0;
        break; }
    case FECHAR:
    case FECHAR_ERRO:
        g.esperando = mcClose(g.fd) == 0;
        /* recusado: tenta de novo nos próximos quadros (o arquivo continua aberto) */
        if (!g.esperando && ++g.recusas >= 10) { g.fd = -1; termina(g.etapa == FECHAR_ERRO ? g.erro : -1); }
        return;
    case APAGA_ANTIGO:
        g.esperando = mcDelete(g.porta, 0, g.caminho) == 0;
        break;
    case LIMPA_TMP:
        g.esperando = mcDelete(g.porta, 0, g.tmp) == 0;
        if (!g.esperando) proximo_arquivo();
        return;
    }
    if (!g.esperando) falha(-1);    /* a libmc recusou mandar o pedido */
}
