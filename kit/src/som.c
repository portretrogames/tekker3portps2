/* kit/src/som.c — ver kit/som.h. */
#include "kit/som.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <audsrv.h>
#include "kit/diagnostico.h"     /* kit_relogio, kit_ms_desde */

/* gerados por bin2c (kit/kit.mk): freesd do PS2SDK, audsrv de kit/iop/audsrv */
extern unsigned char freesd_irx[], audsrv_irx[];
extern unsigned int size_freesd_irx, size_audsrv_irx;

static int pronto;
static unsigned capacidade;      /* bytes que cabem na fila do IOP (20480 a 48 kHz, ~107 ms) */
static char estado[96] = "som: nao iniciado";

/* Envio sem esperar (kit_som_mandar): cliente RPC próprio ligado ao audsrv
 * do IOP, pacote no formato do AUDSRV_PLAY_AUDIO ([0] = bytes, depois o
 * PCM) e resposta (bytes aceitos) num buffer só dela, lida sem cache. */
#define RPC_PLAY_AUDIO 0x0004    /* AUDSRV_PLAY_AUDIO (kit/iop/audsrv/include/audsrv.h) */
static SifRpcClientData_t cli __attribute__((aligned(64)));
/* tamanho em múltiplo de 64 bytes: a DMA da SIF lê de 16 em 16 bytes */
static int32_t pacote[(1u + KIT_SOM_LOTE + 15u) & ~15u] __attribute__((aligned(64)));
static int32_t resposta[16] __attribute__((aligned(64)));   /* 64 bytes: a DMA escreve 16 */
static volatile int em_voo;      /* 1 = o IOP ainda não respondeu */
static unsigned voo_q;           /* quadros do envio em voo (ou já respondido e não lido) */
static unsigned long erros_rpc;

static void fim_envio(void *p) { (void)p; em_voo = 0; }   /* roda na interrupção da SIF */

/* 0.3.0: canal da SPU do PS1 (cliente próprio, servidor próprio no IOP) */
#define PS1_SID 0x8708850         /* AUDSRV_PS1_SID: servidor próprio (o SIF RPC só aguenta
                                  * uma chamada em andamento por servidor) */
static SifRpcClientData_t cli1 __attribute__((aligned(64)));
static uint32_t pacote1[KIT_SPU1_PALAVRAS] __attribute__((aligned(64)));
static uint32_t resposta1[16] __attribute__((aligned(64)));
static volatile int em_voo1;
static int pronto1;
static volatile int resposta1_nova;   /* escrita na interrupção da SIF */
static void fim_envio1(void *p) { (void)p; resposta1_nova = 1; em_voo1 = 0; }

/* Liga um cliente ao servidor do audsrv sem esperar dentro da chamada, com
 * prazo (ver a nota em kit_som_iniciar). */
static int ligar(SifRpcClientData_t *c, unsigned sid, unsigned prazo_ms)
{
    uint32_t ini = kit_relogio();
    for (;;) {
        /* com um pedido ainda pendente (de uma tentativa anterior) não
         * reenvia: a resposta atrasada liberaria o pacote do novo */
        int pedido = SifCheckStatRpc(c) ? 1 : SifBindRpc(c, (int)sid, SIF_RPC_M_NOWAIT) >= 0;
        while (pedido && SifCheckStatRpc(c) && kit_ms_desde(ini) < prazo_ms) nopdelay();
        if (pedido && !SifCheckStatRpc(c) && c->server != NULL) return 1;
        if (kit_ms_desde(ini) >= prazo_ms) return 0;
        nopdelay();
    }
}

static int carregar(const char *nome, unsigned char *irx, unsigned int tam)
{
    int res = 0, id = SifExecModuleBuffer(irx, tam, 0, NULL, &res);
    if (id < 0 || res == 1) {
        snprintf(estado, sizeof(estado), "som: %s falhou (id %d, res %d)", nome, id, res);
        printf("%s\n", estado);
        return 0;
    }
    printf("iop: %s carregado\n", nome);
    return 1;
}

/* 0.3.1: cada passo da inicialização vai para o log na hora (no console um
 * travamento aqui não deixava rastro) */
static void passo(const char *t)
{
    printf("som: passo %s\n", t);
    kit_log_gravar();
}

int kit_som_iniciar(int freq)
{
    struct audsrv_fmt_t fmt;
    int r;
    if (pronto) return 1;
    passo("SifInitRpc");
    SifInitRpc(0);
    passo("sbv_patch_enable_lmb");
    sbv_patch_enable_lmb();
    passo("carregar freesd");            /* SifExecModuleBuffer (no host: o IOP não foi reiniciado) */
    if (!carregar("freesd", freesd_irx, size_freesd_irx)) return 0;
    passo("carregar audsrv");
    if (!carregar("audsrv", audsrv_irx, size_audsrv_irx)) return 0;
    passo("audsrv_init");
    if ((r = audsrv_init()) != 0) {
        snprintf(estado, sizeof(estado), "som: audsrv_init devolveu %d", r);
        printf("%s\n", estado);
        return 0;
    }
    passo("audsrv_set_format");
    fmt.freq = freq; fmt.bits = 16; fmt.channels = 2;
    if ((r = audsrv_set_format(&fmt)) != 0) {
        snprintf(estado, sizeof(estado), "som: formato %d Hz recusado (%d)", freq, r);
        printf("%s\n", estado);
        return 0;
    }
    passo("audsrv_set_volume");
    if ((r = audsrv_set_volume(MAX_VOLUME)) != 0) {
        snprintf(estado, sizeof(estado), "som: volume recusado (%d)", r);
        printf("%s\n", estado);
        return 0;
    }
    /* Capacidade = livre + na fila: o audsrv_set_format já deixa metade da
     * fila "cheia" (readpos no meio), então o livre sozinho dava a metade
     * (10240 de 20480 bytes a 48 kHz; revisão V47). As duas perguntas não
     * brigam com o consumo: até o 1º audsrv_play_audio o IOP não anda na
     * fila (playing = 0). */
    {
        int livre, fila;
        passo("audsrv_available/queued");
        livre = audsrv_available(); fila = audsrv_queued();
        if (livre < 0 || fila < 0) {
            snprintf(estado, sizeof(estado), "som: audsrv_available/queued devolveu %d/%d", livre, fila);
            printf("%s\n", estado);
            return 0;
        }
        capacidade = (unsigned)(livre + fila);
    }
    /* cliente RPC próprio para o envio sem esperar (kit_som_mandar) */
    /* 0.2.1: com prazo (antes laço sem fim se o servidor não subisse).
     * O bind é pedido sem esperar (SifBindRpc com 0 espera a resposta por
     * dentro e o prazo não pegaria uma chamada presa); cli é estático,
     * então uma resposta atrasada não escreve em memória solta. O
     * servidor já respondeu ao audsrv_init, então na prática liga logo. */
    passo("ligar canal do PCM");
    if (!ligar(&cli, AUDSRV_IRX, 2000u)) {
        snprintf(estado, sizeof(estado), "som: servidor RPC do audsrv nao respondeu");
        printf("%s\n", estado);
        /* sem audsrv_quit: ele também espera o IOP e travaria aqui */
        return 0;
    }
    /* 0.3.0: canal da SPU do PS1; sem ele o PCM segue normal */
    passo("ligar canal da SPU");
    pronto1 = ligar(&cli1, PS1_SID, 2000u);
    if (!pronto1) printf("som: canal da SPU do PS1 nao ligou\n");
    snprintf(estado, sizeof(estado), "som: audsrv %d Hz, fila %u bytes", freq, capacidade);
    printf("%s\n", estado);
    pronto = 1;
    return 1;
}

int kit_som_pronto(void) { return pronto; }

static unsigned livre_q;          /* quadros livres no IOP pelo último kit_som_livre() */

unsigned kit_som_livre(void)
{
    int a;
    if (!pronto) return 0;
    a = audsrv_available();
    livre_q = a > 0 ? (unsigned)a / 4u : 0u;
    return livre_q;
}

unsigned kit_som_na_fila(void)
{
    unsigned cap = capacidade / 4u;
    return pronto && cap > livre_q ? cap - livre_q : 0u;
}

unsigned kit_som_enviar(const int16_t *pcm, unsigned quadros)
{
    if (!pronto) return 0;
    if (quadros > livre_q) quadros = livre_q;
    if (quadros) {
        int r = audsrv_play_audio((const char *)pcm, (int)(quadros * 4u));
        quadros = r > 0 ? (unsigned)r / 4u : 0u;   /* o que o IOP aceitou */
        livre_q -= quadros < livre_q ? quadros : livre_q;
    }
    return quadros;
}

void kit_som_completar(unsigned alvo)
{
    static int16_t zeros[2 * 1024];    /* uma chamada só na maior parte das vezes */
    unsigned fila = kit_som_na_fila();
    while (pronto && fila < alvo) {
        unsigned n = alvo - fila;
        if (n > 1024u) n = 1024u;
        n = kit_som_enviar(zeros, n);
        if (!n) break;
        fila += n;
    }
}

int kit_som_concluir(void)
{
    int bytes;
    if (!pronto) return 0;
    if (em_voo) return -1;
    if (!voo_q) return 0;
    voo_q = 0;
    bytes = *(volatile int32_t *)UNCACHED_SEG(&resposta[0]);
    if (bytes < 0) { erros_rpc++; return 0; }
    return bytes / 4;
}

/* Manda os n quadros já postos em pacote[1..n]. */
static unsigned disparar(unsigned n)
{
    pacote[0] = (int32_t)(n * 4u);
    em_voo = 1;
    voo_q = n;
    /* SifCallRpc devolve logo (NOWAIT); grava o cache do pacote e da
     * resposta na memória antes da DMA. fim_envio() marca a volta. */
    if (SifCallRpc(&cli, RPC_PLAY_AUDIO, SIF_RPC_M_NOWAIT, pacote, (int)(4u + n * 4u),
                   resposta, 4, fim_envio, NULL) < 0) {
        em_voo = 0; voo_q = 0; erros_rpc++;
        return 0;
    }
    return n;
}

unsigned kit_som_mandar(const int16_t *a, unsigned qa, const int16_t *b, unsigned qb)
{
    if (!pronto || em_voo || voo_q) return 0;
    if (qa > KIT_SOM_LOTE) qa = KIT_SOM_LOTE;
    if (qb > KIT_SOM_LOTE - qa) qb = KIT_SOM_LOTE - qa;
    if (!qa && !qb) return 0;
    if (qa) memcpy(&pacote[1], a, qa * 4u);
    if (qb) memcpy(&pacote[1u + qa], b, qb * 4u);
    return disparar(qa + qb);
}

unsigned kit_som_mandar_silencio(unsigned quadros)
{
    if (!pronto || em_voo || voo_q || !quadros) return 0;
    if (quadros > KIT_SOM_LOTE) quadros = KIT_SOM_LOTE;
    memset(&pacote[1], 0, quadros * 4u);
    return disparar(quadros);
}

const char *kit_som_estado(void)
{
    if (pronto && erros_rpc)
        snprintf(estado, sizeof(estado), "som: audsrv, fila %u bytes, %lu erros de envio",
                 capacidade, erros_rpc);
    return estado;
}

/* ---- 0.3.0: SPU do PS1 ---------------------------------------------- */
int kit_spu1_pronto(void) { return pronto && pronto1; }
int kit_spu1_ocupado(void) { return em_voo1; }

int kit_spu1_enviar(const uint32_t *p, unsigned palavras)
{
    if (!p || !pronto || !pronto1 || em_voo1 || !palavras || palavras > KIT_SPU1_PALAVRAS) return 0;
    memcpy(pacote1, p, palavras * 4u);
    em_voo1 = 1;
    /* NOWAIT: SifCallRpc grava o cache do pacote e da resposta antes da DMA */
    if (SifCallRpc(&cli1, 0, SIF_RPC_M_NOWAIT, pacote1, (int)(palavras * 4u),
                   resposta1, 64, fim_envio1, NULL) < 0) {
        em_voo1 = 0; erros_rpc++;
        return 0;
    }
    return 1;
}

void kit_spu1_esperar(void)
{
    uint32_t ini = kit_relogio();
    while (em_voo1 && kit_ms_desde(ini) < 1000u) nopdelay();
}

int kit_spu1_resposta_nova(kit_spu1_resposta *r)
{
    volatile uint32_t *u = (volatile uint32_t *)UNCACHED_SEG(&resposta1[0]);
    unsigned v;
    if (em_voo1 || !resposta1_nova) return 0;
    resposta1_nova = 0;
    r->endx = u[0];
    for (v = 0; v < 24u; v += 2u) {
        uint32_t e = u[1u + v / 2u];
        r->envx[v] = (uint16_t)e;
        r->envx[v + 1u] = (uint16_t)(e >> 16);
    }
    r->pacotes = u[13];
    r->erros = u[14];
    return 1;
}
