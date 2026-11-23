/*
 * spu.c — SPU do PS1 tocada na SPU2 do PS2 (V53).
 *
 * O jogo escreve nos registradores da SPU (io.c -> som.c -> aqui) e manda
 * amostras ADPCM pela DMA 4 ou pela porta de dados (0x1F801DA8). Aqui:
 * - a memória da SPU (512 KB) fica numa cópia no EE (leituras pela DMA e o
 *   envio em blocos de 64 bytes, alinhados como a DMA da SPU2 quer);
 * - cada escrita vira um comando de um pacote (formato em
 *   kit/iop/audsrv/src/ps1spu.c), na ordem em que o jogo fez (amostras antes
 *   do KON que as toca);
 * - uma vez por VBlank o pacote vai para o IOP sem esperar (kit_spu1_enviar);
 *   no VBlank seguinte chega a resposta: ENDX e ENVX das vozes, que o
 *   sequenciador do jogo lê para saber quais vozes estão livres.
 * Sem a saída de som (PCSX2 sem o kit, testes no PC) nada é enviado e o
 * ENDX continua "todas terminaram", como antes da V53.
 */
#include "spu.h"
#include "ram.h"
#include <stdio.h>
#include <string.h>
#ifdef _EE
#include "kit/som.h"
#include "kit/diagnostico.h"     /* kit_relogio, kit_ms_desde */
#endif

#define RAM_SPU 0x80000u

static uint8_t ram_spu[RAM_SPU] __attribute__((aligned(64)));
static uint32_t end_transf;            /* endereço da porta/DMA em bytes */
static uint32_t suja_ini, suja_fim;    /* faixa escrita ainda não posta no pacote */
static uint32_t pac[4096];             /* KIT_SPU1_PALAVRAS */
static unsigned pac_n;
static uint32_t endx = 0xFFFFFFu;      /* espelho do ENDX (bit = voz terminou) */
static uint32_t kon_desde_envio;
static uint16_t envx[24];
static uint32_t spu_vb, kon_vb[24];
/*
 * V57: ENVX de voz recém-ligada. O jogo lê o ENVX das 24 vozes a cada quadro
 * (~24 leituras por VBlank; o ENDX nunca) e o IOP lê o ENVX microssegundos
 * depois de gravar o KON, quando o envelope ainda é 0: no quadro seguinte o
 * jogo via a voz nova com envelope 0 (no PS1, ~735 amostras depois do KON, o
 * ataque já subiu). Medido no PCSX2: 130 leituras assim em 252 KONs (ANALISE, V57).
 * kon_sem_env: vozes ligadas cuja resposta com o envelope de verdade ainda
 * não chegou. No mesmo VBlank do KON a leitura dá 0 (como no PS1 logo
 * depois do KON); nos seguintes, até a resposta certa, dá ENVX_ATAQUE.
 * voo_kon_antigo: KONs que o pacote em voo já pega com o envelope andando
 * (ligadas num pacote anterior a ele).
 */
#define ENVX_ATAQUE 0x7FFFu
static uint32_t kon_sem_env;
static uint32_t kon_neste_vb;           /* KONs deste VBlank (zerado em ps2_spu_vblank) */
#ifdef _EE
static uint32_t voo_kon_antigo;
#endif
#ifdef _EE
static int ativo = -1;                  /* -1 = ainda não perguntou ao kit */
#endif

static void sujar(uint32_t a, uint32_t n);

#ifdef _EE
static uint32_t envio_ini;              /* relógio do EE quando o pacote em voo saiu */
#endif



static int canal_pronto(void)
{
#ifdef _EE
    if (ativo == 0 || ativo == -1) ativo = kit_spu1_pronto();
    return ativo > 0;
#else
    return 0;
#endif
}

/* canal ainda pode ligar (a faixa escrita antes fica guardada para ir junto) */
static int canal_possivel(void)
{
#ifdef _EE
    return ativo != -2;
#else
    return 0;
#endif
}

/* O IOP parou de responder (nem esperando ~1 s): desliga o canal de vez em
 * vez de segurar o jogo; o ENDX volta a "todas terminaram". */
static __attribute__((unused)) void desligar_canal(void)
{
#ifdef _EE
    ativo = -2;
#endif
    pac_n = 0;
    suja_ini = suja_fim = 0;
}

#ifdef _EE
/* Lê a resposta do pacote anterior, se já chegou. Chamada no VBlank e antes
 * de cada envio: um envio na hora do KON logo depois de a resposta chegar
 * a sobrescreveria sem ler (o ENDX/ENVX ficava parado). */
static void consumir_resposta(void)
{
    kit_spu1_resposta r;
    if (!kit_spu1_resposta_nova(&r)) return;
    /* vozes ligadas depois do envio ainda não estão na resposta */
    endx = r.endx & ~kon_desde_envio;
    memcpy(envx, r.envx, sizeof(envx));
    /* estas vieram com o envelope de verdade, menos as religadas depois do
     * envio (KON de novo na mesma voz: a proteção vale para o KON novo) */
    kon_sem_env &= ~(voo_kon_antigo & ~kon_desde_envio);
    voo_kon_antigo = 0;
    {   /* voz religada depois do envio: a resposta é da ativação anterior */
        uint32_t m = kon_desde_envio;
        while (m) { envx[__builtin_ctz(m)] = 0; m &= m - 1u; }
    }
}
#endif

/* Manda o pacote montado (esperando o anterior, se preciso). 1 = foi. */
static int enviar(int esperar)
{
#ifdef _EE
    if (!pac_n) return 0;
    if (kit_spu1_ocupado()) {
        if (!esperar) return 0;
        kit_spu1_esperar();
        if (kit_spu1_ocupado()) { desligar_canal(); return 0; }
    }
    consumir_resposta();
    if (kit_spu1_enviar(pac, pac_n)) {
        envio_ini = kit_relogio();
        /* só KONs de um VBlank anterior: um pacote mandado logo depois do KON
         * (dois envios no mesmo quadro) ainda pegaria o ataque perto de 0 */
        voo_kon_antigo = kon_sem_env & ~kon_desde_envio & ~kon_neste_vb;
        kon_desde_envio = 0;
        pac_n = 0;
        return 1;
    }
    if (esperar) desligar_canal();
    return 0;
#else
    (void)esperar;
    pac_n = 0;
    return 1;
#endif
}

/* Garante espaço para n palavras no pacote (manda e espera se precisar). */
static void reservar(unsigned n)
{
    if (pac_n + n > sizeof(pac) / 4u) enviar(1);
    /* (se o IOP não respondeu, desligar_canal já esvaziou o pacote) */
}

/* Põe no pacote a faixa suja da memória, em blocos de 64 bytes de até
 * 8128 bytes (dois cabem num pacote de 4096 palavras) */
static void postar_dados(void)
{
    uint32_t a, b;
    /* canal ainda desligado: a faixa fica guardada até ele ligar */
    if (suja_ini >= suja_fim || !canal_pronto()) return;
    a = suja_ini & ~63u;
    b = (suja_fim + 63u) & ~63u;
    if (b > RAM_SPU) b = RAM_SPU;
    suja_ini = suja_fim = 0;
    while (a < b) {
        uint32_t n = b - a < 8128u ? b - a : 8128u;
        reservar(2u + n / 4u);
        if (!canal_pronto()) return;    /* desligou esperando o IOP */
        pac[pac_n++] = (2u << 28) | (a >> 3);
        pac[pac_n++] = n;
        memcpy(&pac[pac_n], &ram_spu[a], n);
        pac_n += n / 4u;
        a += n;
    }
}

static void sujar(uint32_t a, uint32_t n)
{
    if (!n) return;
    /* faixa que não encosta na anterior: posta a anterior primeiro */
    if (suja_ini < suja_fim && (a > suja_fim || a + n < suja_ini)) postar_dados();
    if (suja_ini >= suja_fim) { suja_ini = a; suja_fim = a + n; }
    else {
        if (a < suja_ini) suja_ini = a;
        if (a + n > suja_fim) suja_fim = a + n;
    }
}

static void reg16(uint32_t off, uint16_t v)
{
    if (off == 0xDA6u) end_transf = ((uint32_t)v * 8u) & (RAM_SPU - 1u);
    else if (off == 0xDA8u) {           /* porta de dados: 1 halfword por escrita */
        memcpy(&ram_spu[end_transf], &v, 2);
        if (canal_possivel()) sujar(end_transf, 2);
        end_transf = (end_transf + 2u) & (RAM_SPU - 1u);
        return;
    }
    if (canal_pronto()) {
        postar_dados();                 /* amostras antes do registrador */
        /* KON/KOFF baixo: guarda lugar para a metade alta no mesmo pacote
         * (o IOP junta as duas; em pacotes separados a 2ª zeraria a 1ª) */
        reservar(off == 0xD88u || off == 0xD8Cu ? 2u : 1u);
    }
    /* KON: a voz deixa de estar "terminada" já; marcada depois de reservar
     * (um envio forçado ali zera kon_desde_envio do pacote anterior) */
    if (off == 0xD88u || off == 0xD8Au) {
        uint32_t m = off == 0xD88u ? v : (uint32_t)(v & 0xFFu) << 16;
        endx &= ~m; kon_desde_envio |= m;
        kon_sem_env |= m; kon_neste_vb |= m;
        while (m) { unsigned b = (unsigned)__builtin_ctz(m); kon_vb[b] = spu_vb; envx[b] = 0; m &= m - 1u; }
    }
    if (!canal_pronto()) return;
    pac[pac_n++] = (1u << 28) | (((off - 0xC00u) >> 1) << 16) | v;
}

void ps2_spu_escrita(uint32_t off, uint32_t v, unsigned w)
{
    /* registradores graváveis: 0xC00-0xDFF (de 0xE00 em diante só leitura) */
    if (off < 0xC00u || off + w > 0xE00u) return;
    if (w == 4u) { reg16(off, (uint16_t)v); reg16(off + 2u, (uint16_t)(v >> 16)); }
    else if (w == 2u) reg16(off, (uint16_t)v);
    /* escrita de 1 byte nos registradores da SPU: o PS1 não faz; ignora */
    /* V54: o jogo ligou vozes (metade alta do KON, ou o KON inteiro de 32
     * bits): manda o pacote já, sem esperar o VBlank (até ~17 ms de atraso no
     * som). Depois da metade alta porque o driver grava a baixa e logo a alta;
     * se estiver ocupado, vai no VBlank. */
    /* (o driver grava o KON todo quadro, quase sempre com 0: só manda se
     * alguma voz foi de fato ligada desde o último envio) */
    if (w == 1u || !kon_desde_envio) return;
    if (off == 0xD8Au || (off == 0xD88u && w == 4u)) {
        enviar(0);
    }
}

int ps2_spu_leitura(uint32_t off, unsigned w, uint32_t *v)
{
    /* sem a SPU2: todas as vozes "já terminaram" (com 0 o driver de som
     * achava que nada acabava e a tela de título ficava esperando) */
    uint32_t e = canal_pronto() ? endx : 0xFFFFFFu;
    if (off < 0xC00u) return 0;
    if (off == 0xD9Cu) { *v = w == 4u ? e : (e & 0xFFFFu); return 1; }
    if (off == 0xD9Eu) { *v = (e >> 16) & 0xFFu; return 1; }
    if (off < 0xD80u && (off & 0xFu) == 0xCu && canal_pronto()) {
        unsigned voz = (off - 0xC00u) >> 4;
        uint32_t e_voz = envx[voz];
        if (((kon_sem_env >> voz) & 1u) && spu_vb != kon_vb[voz]) e_voz = ENVX_ATAQUE;
        *v = e_voz;
        return 1;
    }
    return 0;
}

void ps2_spu_dma4(uint32_t madr, uint32_t bcr, uint32_t chcr)
{
    uint32_t bs = bcr & 0xFFFFu, bc = bcr >> 16;
    uint32_t n = bs * (bc ? bc : 1u) * 4u, k;
    madr &= PS2_RAM_MASK & ~3u;         /* a DMA anda em palavras: alinha só aqui */
    for (k = 0; k < n; ) {
        uint32_t pedaco = n - k;
        if (pedaco > RAM_SPU - end_transf) pedaco = RAM_SPU - end_transf;
        if (pedaco > PS2_RAM_SIZE - madr) pedaco = PS2_RAM_SIZE - madr;
        if (chcr & 1u) {                /* RAM -> SPU */
            memcpy(&ram_spu[end_transf], &g_ps1_ram[madr], pedaco);
            if (canal_possivel()) sujar(end_transf, pedaco);
        } else {                        /* SPU -> RAM */
            memcpy(&g_ps1_ram[madr], &ram_spu[end_transf], pedaco);
            ps2_cod_marcar_faixa(madr, pedaco);   /* código nativo dessa faixa fica velho */
        }
        k += pedaco;
        end_transf = (end_transf + pedaco) & (RAM_SPU - 1u);
        madr = (madr + pedaco) & PS2_RAM_MASK;
    }
    if ((chcr & 1u) && canal_pronto()) postar_dados();
}

void ps2_spu_vblank(void)
{
    spu_vb++;
    kon_neste_vb = 0;
#ifdef _EE
    if (!canal_pronto()) return;
    /* pacote em voo há mais de 1 s: o IOP travou; desliga em vez de deixar o
     * ENDX/ENVX parados para sempre */
    if (kit_spu1_ocupado() && kit_ms_desde(envio_ini) > 1000u) { desligar_canal(); return; }
    consumir_resposta();
    postar_dados();
    /* quadro sem comandos: manda um "nada" para o ENDX/ENVX não ficar parado */
    if (!pac_n && canal_pronto()) pac[pac_n++] = 0u;
    enviar(0);
#endif
}
