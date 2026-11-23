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
/* relatório */
static unsigned long r_pacotes, r_palavras, r_maior, r_ocupado, r_respostas, r_esperas;
static unsigned long r_bytes_dados, r_reverb, r_eon, r_kon, r_perdidos, r_kon_imediato;
/* V57: o jogo usa reverberação? lê o envelope? (base da área do reverb,
 * SPUCNT com o bit 7 ligado; leituras de ENVX e de ENDX) */
static unsigned long r_mbase, r_spucnt_rev, r_le_envx, r_le_endx;
/* ENVX 0 lido numa voz ligada (KON) há no máximo 2 VBlanks: no PS1 o envelope
 * já teria subido; aqui é a resposta velha (ou lida logo depois do KON) */
static unsigned long r_envx0_recente;
static uint32_t spu_vb, kon_vb[24], kon_visto;
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
#ifndef PS2_ENVX_ATAQUE
#define PS2_ENVX_ATAQUE 1               /* 0 = como antes (para comparar) */
#endif
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
static unsigned long r_erros;
#endif

#ifndef PS2_SPU2_LIGADA
#define PS2_SPU2_LIGADA 1
#endif

#ifdef PS2_SPU_TRACE
/* make SPU_TRACE=1: grava por VBlank o KON/KOFF que o jogo escreveu e o
 * ENDX/ENVX que ele leu, e os registradores de cada voz ligada; aos
 * TR_QUADROS VBlanks salva em host:spu_trace.bin (comparar console x PCSX2
 * com tools/comparar_spu_trace.py). */
#define TR_QUADROS 5400u
#define TR_KONS 8192u
typedef struct { uint32_t vb, kon, koff, endx; uint16_t envx[24]; uint32_t cd_leituras, cd_espera, dma4; } tr_quadro;
extern unsigned long g_ps2_cd_leituras_arquivo;
extern unsigned long long g_ps2_cd_espera;
static uint32_t tr_dma4;
typedef struct { uint32_t vb; uint16_t voz, pitch, ssa, adsr1, adsr2, voll, volr, lsa; } tr_kon;
static tr_quadro tr_q[TR_QUADROS];
static tr_kon tr_k[TR_KONS];
static unsigned tr_nq, tr_nk;
static uint16_t tr_reg[0x200];
static uint32_t tr_vb, tr_kon_q, tr_koff_q;

static void tr_escrita(uint32_t off, uint16_t v)
{
    uint32_t m;
    tr_reg[((off - 0xC00u) >> 1) & 0x1FFu] = v;
    if (off == 0xD88u || off == 0xD8Au) {
        m = off == 0xD88u ? v : (uint32_t)(v & 0xFFu) << 16;
        tr_kon_q |= m;
        for (unsigned voz = 0; voz < 24; voz++) {
            const uint16_t *r = &tr_reg[voz * 8u];
            if (!(m & (1u << voz)) || tr_nk >= TR_KONS) continue;
            tr_k[tr_nk].vb = tr_vb; tr_k[tr_nk].voz = (uint16_t)voz;
            tr_k[tr_nk].voll = r[0]; tr_k[tr_nk].volr = r[1]; tr_k[tr_nk].pitch = r[2];
            tr_k[tr_nk].ssa = r[3]; tr_k[tr_nk].adsr1 = r[4]; tr_k[tr_nk].adsr2 = r[5];
            tr_k[tr_nk].lsa = r[7];
            tr_nk++;
        }
    } else if (off == 0xD8Cu) tr_koff_q |= v;
    else if (off == 0xD8Eu) tr_koff_q |= (uint32_t)(v & 0xFFu) << 16;
}

static void tr_vblank(void)
{
    if (tr_nq < TR_QUADROS) {
        tr_quadro *q = &tr_q[tr_nq++];
        q->vb = tr_vb; q->kon = tr_kon_q; q->koff = tr_koff_q; q->endx = endx;
        memcpy(q->envx, envx, sizeof(q->envx));
        q->cd_leituras = (uint32_t)g_ps2_cd_leituras_arquivo;
        q->cd_espera = (uint32_t)(g_ps2_cd_espera >> 10);   /* ciclos/1024 */
        q->dma4 = tr_dma4;
    }
    tr_kon_q = tr_koff_q = 0;
    if (++tr_vb == TR_QUADROS) {
        FILE *f = fopen("host:spu_trace.bin", "wb");
        if (f) {
            uint32_t cab[4] = { 0x54555053u, tr_nq, tr_nk, 2u };
            fwrite(cab, sizeof(cab), 1, f);
            fwrite(tr_q, sizeof(tr_q[0]), tr_nq, f);
            fwrite(tr_k, sizeof(tr_k[0]), tr_nk, f);
            fclose(f);
        }
    }
}
#endif

static int canal_pronto(void)
{
#if defined(_EE) && PS2_SPU2_LIGADA
    if (ativo == 0 || ativo == -1) ativo = kit_spu1_pronto();
    return ativo > 0;
#else
    return 0;
#endif
}

/* canal ainda pode ligar (a faixa escrita antes fica guardada para ir junto) */
static int canal_possivel(void)
{
#if defined(_EE) && PS2_SPU2_LIGADA
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
    if (ativo > 0) printf("spu2: o IOP nao respondeu, efeitos desligados\n");
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
    if (r.erros != r_erros) {
        printf("spu2: a SPU2 recusou %lu blocos de amostras\n", (unsigned long)r.erros);
        r_erros = r.erros;
    }
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
    r_respostas++;
}
#endif

/* Manda o pacote montado (esperando o anterior, se preciso). 1 = foi. */
static int enviar(int esperar)
{
#ifdef _EE
    if (!pac_n) return 0;
    if (kit_spu1_ocupado()) {
        if (!esperar) { r_ocupado++; return 0; }
        r_esperas++;
        kit_spu1_esperar();
        if (kit_spu1_ocupado()) { r_perdidos += pac_n; desligar_canal(); return 0; }
    }
    consumir_resposta();
    if (kit_spu1_enviar(pac, pac_n)) {
        envio_ini = kit_relogio();
        /* só KONs de um VBlank anterior: um pacote mandado logo depois do KON
         * (dois envios no mesmo quadro) ainda pegaria o ataque perto de 0 */
        voo_kon_antigo = kon_sem_env & ~kon_desde_envio & ~kon_neste_vb;
        r_pacotes++; r_palavras += pac_n;
        if (pac_n > r_maior) r_maior = pac_n;
        kon_desde_envio = 0;
        pac_n = 0;
        return 1;
    }
    if (esperar) { r_perdidos += pac_n; desligar_canal(); }
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
        r_bytes_dados += n;
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
#ifdef PS2_SPU_TRACE
    if (off != 0xDA8u) tr_escrita(off, v);
#endif
    if (off == 0xDA6u) end_transf = ((uint32_t)v * 8u) & (RAM_SPU - 1u);
    else if (off == 0xDA8u) {           /* porta de dados: 1 halfword por escrita */
        memcpy(&ram_spu[end_transf], &v, 2);
        if (canal_possivel()) sujar(end_transf, 2);
        end_transf = (end_transf + 2u) & (RAM_SPU - 1u);
        return;
    }
    if (off >= 0xDC0u) r_reverb++;
    if ((off == 0xD98u || off == 0xD9Au) && v) r_eon++;
    if (off == 0xDA2u) r_mbase++;
    if (off == 0xDAAu && (v & 0x80u)) r_spucnt_rev++;
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
        kon_visto |= m; kon_sem_env |= m; kon_neste_vb |= m;
        while (m) { unsigned b = (unsigned)__builtin_ctz(m); kon_vb[b] = spu_vb; envx[b] = 0; r_kon++; m &= m - 1u; }
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
        if (enviar(0)) r_kon_imediato++;
    }
}

int ps2_spu_leitura(uint32_t off, unsigned w, uint32_t *v)
{
    /* sem a SPU2: todas as vozes "já terminaram" (com 0 o driver de som
     * achava que nada acabava e a tela de título ficava esperando) */
    uint32_t e = canal_pronto() ? endx : 0xFFFFFFu;
    if (off < 0xC00u) return 0;
    if (off == 0xD9Cu) { r_le_endx++; *v = w == 4u ? e : (e & 0xFFFFu); return 1; }
    if (off == 0xD9Eu) { r_le_endx++; *v = (e >> 16) & 0xFFu; return 1; }
    if (off < 0xD80u && (off & 0xFu) == 0xCu && canal_pronto()) {
        unsigned voz = (off - 0xC00u) >> 4;
        uint32_t e_voz = envx[voz];
        if (PS2_ENVX_ATAQUE && ((kon_sem_env >> voz) & 1u) && spu_vb != kon_vb[voz]) e_voz = ENVX_ATAQUE;
        r_le_envx++;
        if (!e_voz && ((kon_visto >> voz) & 1u) && spu_vb != kon_vb[voz] && spu_vb - kon_vb[voz] <= 2u)
            r_envx0_recente++;
        *v = e_voz;
        return 1;
    }
    return 0;
}

void ps2_spu_dma4(uint32_t madr, uint32_t bcr, uint32_t chcr)
{
    uint32_t bs = bcr & 0xFFFFu, bc = bcr >> 16;
    uint32_t n = bs * (bc ? bc : 1u) * 4u, k;
#ifdef PS2_SPU_TRACE
    tr_dma4 += n;
#endif
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
#ifdef PS2_SPU_TRACE
    tr_vblank();                        /* (o ENDX/ENVX deste quadro: os da resposta anterior) */
#endif
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

void ps2_spu_relatorio(void)
{
    printf("spu2: %s, pacotes %lu (%lu palavras, maior %lu), dados %lu bytes, kon %lu, respostas %lu, "
           "ocupado %lu, esperas %lu, perdidas %lu palavras, envios no KON %lu, reverb: escritas %lu, eon %lu, "
           "mbase %lu, spucnt com reverb %lu | leituras: envx %lu (0 em voz recem-ligada %lu), endx %lu\n",
           canal_pronto() ? "ligada" : "DESLIGADA", r_pacotes, r_palavras, r_maior, r_bytes_dados, r_kon,
           r_respostas, r_ocupado, r_esperas, r_perdidos, r_kon_imediato, r_reverb, r_eon,
           r_mbase, r_spucnt_rev, r_le_envx, r_envx0_recente, r_le_endx);
    r_pacotes = r_palavras = r_maior = r_ocupado = r_respostas = r_esperas = 0;
    r_bytes_dados = r_reverb = r_eon = r_kon = r_perdidos = r_kon_imediato = 0;
    r_mbase = r_spucnt_rev = r_le_envx = r_le_endx = r_envx0_recente = 0;
}
