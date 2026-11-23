/* bios_hle.c — ver bios_hle.h. */
#include "cartao_ps1.h"
#include "pmc.h"
#include "bios_hle.h"
#include "io.h"
#include "cdrom.h"
#include "ram.h"
#include "video_gs.h"
#include "gpu.h"
#include "pad.h"
#include "som.h"
#include "jogo.h"
#include "trace_fmv.h"

#include <stdio.h>
#include <string.h>

typedef struct { uint16_t num; const char *nome; } NomeFn;

/* Nomes das funções mais usadas (fonte: documentação nocash psx-spx). */
static const NomeFn nomes_a0[] = {
    {0x13,"setjmp"},{0x14,"longjmp"},{0x17,"strcmp"},{0x19,"strcpy"},
    {0x1B,"strlen"},{0x25,"toupper"},{0x28,"bzero"},{0x2A,"memcpy"},
    {0x2B,"memset"},{0x2F,"rand"},{0x30,"srand"},{0x33,"malloc"},
    {0x34,"free"},{0x39,"InitHeap"},{0x3F,"printf"},{0x44,"FlushCache"},
    {0x49,"GPU_cw"},{0x70,"_bu_init"},{0x71,"_96_init"},{0x72,"_96_remove"},
    {0x78,"CdAsyncSeekL"},{0x7C,"CdAsyncGetStatus"},{0x7E,"CdAsyncReadSector"},
    {0x95,"CdInitSubFunc"},{0x96,"AddCDROMDevice"},{0x97,"AddMemCardDevice"},
    {0x99,"AddDummyTtyDevice"},{0x9F,"SetMemSize"},{0xA0,"WarmBoot"},
    {0xA1,"SystemErrorBootOrDiskFailure"},{0xA2,"EnqueueCdIntr"},
    {0xA3,"DequeueCdIntr"},{0xAB,"_card_info"},{0xAC,"_card_load"},
    {0xB4,"GetSystemInfo"},
};
static const NomeFn nomes_b0[] = {
    {0x00,"alloc_kernel_memory"},{0x02,"init_timer"},{0x03,"get_timer"},
    {0x04,"enable_timer_irq"},{0x05,"disable_timer_irq"},{0x06,"restart_timer"},
    {0x07,"DeliverEvent"},{0x08,"OpenEvent"},{0x09,"CloseEvent"},
    {0x0A,"WaitEvent"},{0x0B,"TestEvent"},{0x0C,"EnableEvent"},
    {0x0D,"DisableEvent"},{0x0E,"OpenThread"},{0x0F,"CloseThread"},
    {0x10,"ChangeThread"},{0x12,"InitPad"},{0x13,"StartPad"},{0x14,"StopPad"},
    {0x15,"OutdatedPadInitAndStart"},{0x16,"OutdatedPadGetButtons"},
    {0x17,"ReturnFromException"},{0x18,"SetDefaultExitFromException"},
    {0x19,"SetCustomExitFromException"},{0x20,"UnDeliverEvent"},
    {0x32,"open"},{0x33,"lseek"},{0x34,"read"},{0x35,"write"},{0x36,"close"},
    {0x3D,"putchar"},{0x3F,"puts"},{0x42,"firstfile"},{0x43,"nextfile"},
    {0x4A,"InitCard"},{0x4B,"StartCard"},{0x4C,"StopCard"},
    {0x4E,"write_card_sector"},{0x4F,"read_card_sector"},
    {0x50,"allow_new_card"},{0x56,"GetC0Table"},{0x57,"GetB0Table"},
    {0x5B,"ChangeClearPad"},
};
static const NomeFn nomes_c0[] = {
    {0x00,"EnqueueTimerAndVblankIrqs"},{0x01,"EnqueueSyscallHandler"},
    {0x02,"SysEnqIntRP"},{0x03,"SysDeqIntRP"},{0x07,"InstallExceptionHandlers"},
    {0x08,"SysInitMemory"},{0x0A,"ChangeClearRCnt"},{0x0C,"InitDefInt"},
    {0x12,"InstallDevices"},{0x1C,"AdjustA0Table"},
};

static const char *nome_de(const NomeFn *t, unsigned n, uint32_t num)
{
    unsigned i;
    for (i = 0; i < n; i++)
        if (t[i].num == num) return t[i].nome;
    return "?";
}

#define N(x) (sizeof(x) / sizeof((x)[0]))

static uint8_t visto[3][256];

/* ---------------- estado de exceção/interrupção ---------------- */

jmp_buf g_ps2_volta_ao_laco;

static uint32_t custom_exit;          /* endereço do jmp_buf do jogo (0 = nenhum) */
static int      em_excecao;
static int      so_callbacks;          /* exceção "falsa" só para rodar callbacks */
static uint32_t salvo_gpr[32], salvo_hi, salvo_lo, salvo_epc, salvo_sr;

#define CICLOS_VBLANK 565045u          /* 33.868.800 / 59,94 Hz (NTSC) */
static uint32_t proximo_vblank;
static int      vblank_iniciado;
static unsigned long g_vblanks, g_entregas;

/* ---------------- eventos (OpenEvent/TestEvent/DeliverEvent...) ---------------- */

#define EV_MAX      32
#define EV_LIVRE    0x0000u
#define EV_FECHADO  0x1000u   /* aberto, desabilitado */
#define EV_ATIVO    0x2000u   /* habilitado, esperando */
#define EV_PRONTO   0x4000u   /* aconteceu (modo sem callback) */
#define EVMD_INTR   0x1000u   /* modo: chama função */
#define EVMD_NOINTR 0x2000u   /* modo: só marca "pronto" */

typedef struct { uint32_t classe, spec, modo, func, estado; } Evento;
static Evento ev[EV_MAX];

static uint32_t ev_abrir(uint32_t classe, uint32_t spec, uint32_t modo, uint32_t func)
{
    unsigned i;
    for (i = 0; i < EV_MAX; i++)
        if (ev[i].estado == EV_LIVRE) {
            ev[i].classe = classe; ev[i].spec = spec;
            ev[i].modo = modo; ev[i].func = func;
            ev[i].estado = EV_FECHADO;
            return 0xF1000000u | i;
        }
    return 0xFFFFFFFFu;
}

static Evento *ev_de(uint32_t h)
{
    uint32_t i = h & 0xFFFFu;
    return (i < EV_MAX && ev[i].estado != EV_LIVRE) ? &ev[i] : NULL;
}

/* Funções de evento (modo EvMdINTR) a chamar na exceção atual. */
static uint32_t cb_fila[16];
static unsigned cb_n;

static void ev_entregar(uint32_t classe, uint32_t spec)
{
    unsigned i;
    for (i = 0; i < EV_MAX; i++) {
        if (ev[i].estado != EV_ATIVO || ev[i].classe != classe || ev[i].spec != spec)
            continue;
        if (ev[i].modo == EVMD_NOINTR)
            ev[i].estado = EV_PRONTO;
        else if (ev[i].func && cb_n < 16u)
            cb_fila[cb_n++] = ev[i].func;       /* chamada na exceção (ver continuar_excecao) */
    }
}

/*
 * Processamento de exceção, como a BIOS do PS1:
 *   1. salva os registradores;
 *   2. para cada prioridade 0..3, para cada handler registrado com
 *      SysEnqIntRP: chama func1 (struct+8); se devolver != 0 e houver
 *      func2 (struct+4), chama func2(v0);
 *   3. se o jogo registrou SetCustomExitFromException, pula para lá (o
 *      jogo termina com ReturnFromException); senão, restaura e volta.
 * As funções do jogo rodam no próprio laço de despacho: o "retorno" delas
 * cai no endereço mágico RET_MAGICO, que continua esta sequência.
 */
#define RET_MAGICO   0x00000F00u
unsigned long long g_ps2_exc_ciclos; uint32_t g_ps2_exc_ini;
/* tempo e entradas da exceção por origem (bit de I_STAT mais baixo na entrada) */
unsigned long long g_prof_irq_ciclos[11];
unsigned long g_prof_irq_n[11];
static int exc_origem __attribute__((unused));   /* só lido no EE */
#define PILHA_EXC    0x8000F000u      /* pilha da exceção (área do kernel) */

static uint32_t rcnt_clear[4] = {1, 1, 1, 1};   /* ChangeClearRCnt (padrão da BIOS) */
static uint32_t lista_irq[4];          /* SysEnqIntRP: cabeça por prioridade */
static int      exc_prio;              /* prioridade atual */
static uint32_t exc_no;                /* struct atual */
static int      exc_etapa;             /* 0 = vai chamar func1, 1 = chamou func1, 2 = chamou func2, 3 = chamou callback */
/* Etapa e v0 de antes dos callbacks: func1 pode chamar DeliverEvent e os
 * callbacks rodam quando ela volta; depois a sequência continua de onde
 * estava (auditoria V19 1.1: antes func2 se perdia e func1 rodava 2 vezes). */
static int      cb_etapa_salva = -1;
static uint32_t cb_v0_salvo;

extern void ps2_diag(unsigned, uint32_t);
static void chamar_jogo(CPUState *cpu, uint32_t func, uint32_t a0)
{
    ps2_diag(7u, func);
    ps2_timer_pad_adianta(func == JOGO_PAD_VBLANK);
    cpu->gpr[4]  = a0;
    cpu->gpr[29] = PILHA_EXC;
    cpu->gpr[31] = RET_MAGICO;
    cpu->pc      = func;
}

static void sair_da_excecao(CPUState *cpu);
static void return_from_exception(CPUState *cpu);

/* Avança a sequência da exceção; sempre deixa cpu->pc com o próximo passo. */
static void continuar_excecao(CPUState *cpu)
{
    /* 1º: funções de evento (timers/VBlank e DeliverEvent feitos na exceção) */
    if (cb_n > 0u) {
        uint32_t f = cb_fila[0];
        memmove(cb_fila, cb_fila + 1, (cb_n - 1u) * sizeof(cb_fila[0]));
        cb_n--;
        if (exc_etapa != 3) { cb_etapa_salva = exc_etapa; cb_v0_salvo = cpu->gpr[2]; }
        exc_etapa = 3;
        chamar_jogo(cpu, f, 0);
        return;
    }
    if (exc_etapa == 3) {
        if (cb_etapa_salva >= 0) { exc_etapa = cb_etapa_salva; cpu->gpr[2] = cb_v0_salvo; }
        else exc_etapa = 0;
        cb_etapa_salva = -1;
    }
    if (so_callbacks) {                    /* DeliverEvent fora de exceção: acabou */
        so_callbacks = 0;
        return_from_exception(cpu);
        return;
    }
    for (;;) {
        if (exc_etapa == 1) {                       /* voltou de func1 */
            uint32_t func2 = ps2_ram_get32(exc_no + 4);
            static unsigned n_dbg;
            if (n_dbg < 12u) {
                n_dbg++;
                printf("exc: handler %08lx func1=%08lx -> v0=%08lx func2=%08lx I_STAT&MASK=%03lx\n",
                       (unsigned long)exc_no, (unsigned long)ps2_ram_get32(exc_no + 8),
                       (unsigned long)cpu->gpr[2], (unsigned long)func2,
                       (unsigned long)ps2_irq_pending());
            }
            if (cpu->gpr[2] != 0u && func2) {
                exc_etapa = 2;
                chamar_jogo(cpu, func2, cpu->gpr[2]);
                return;
            }
            exc_no = ps2_ram_get32(exc_no);         /* próximo */
            exc_etapa = 0;
        } else if (exc_etapa == 2) {                /* voltou de func2 */
            exc_no = ps2_ram_get32(exc_no);
            exc_etapa = 0;
        }
        /* procura o próximo handler */
        while (exc_no == 0u && exc_prio < 4) {
            exc_no = lista_irq[exc_prio++];
        }
        if (exc_no == 0u) {                         /* acabou a fila */
            sair_da_excecao(cpu);
            return;
        }
        {
            uint32_t func1 = ps2_ram_get32(exc_no + 8);
            if (func1) {
                exc_etapa = 1;
                chamar_jogo(cpu, func1, 0);
                return;
            }
            exc_no = ps2_ram_get32(exc_no);
        }
    }
}

/* V43: timers cuja IRQ esta exceção está atendendo (bit t). io.c usa para
 * reconhecer a escrita no modo feita pela própria callback do timer. */
unsigned g_ps2_tmr_servindo;

/* V47: o laço da espera do quadro (800296C4: chama 8004CE54 e confere
 * 8009BC5C até a IRQ do timer 2 liberar o passo seguinte). Conferência de
 * IRQ com o PC num destes = o jogo está parado esperando.
 * NÃO é tempo livre o laço de 80029A28, que a espera chama antes (com
 * 8009542C ligado): ele faz trabalho do jogo enquanto 80029674 diz que o
 * contador do timer 2 ainda não chegou no limite. Decodificar lá (testado no
 * PCSX2) tirou o tempo do jogo: relógio do round 99% -> 93%. */
#define PC_ESPERA_A 0x80029720u          /* volta do laço */
#define PC_ESPERA_B 0x80029728u          /* retorno de 8004CE54 */
#define PC_ESPERA_C 0x8004CE54u          /* entrada de 8004CE54 (chamada pelo laço: $ra = B) */
extern int g_ps2_contagem_irq;           /* stubs.c */
static int pmc_antes_exc;                 /* região do pmc.h antes da exceção */
static int na_espera(const CPUState *cpu, uint32_t pc)
{
    return pc == PC_ESPERA_A || pc == PC_ESPERA_B ||
           (pc == PC_ESPERA_C && cpu->gpr[31] == PC_ESPERA_B);
}

static void return_from_exception(CPUState *cpu)
{
    ps2_diag(6u, salvo_epc);
    pmc_trocar(pmc_antes_exc);
    g_ps2_tmr_servindo = 0;
    memcpy(cpu->gpr, salvo_gpr, sizeof(salvo_gpr));
    cpu->hi = salvo_hi; cpu->lo = salvo_lo;
    cpu->cop0[12] = salvo_sr;
    cpu->pc = salvo_epc;
    em_excecao = 0;
    /* V47: volta para o laço da espera do quadro (depois da IRQ do VBlank,
     * ~1,2 ms antes da IRQ do timer 2) com música na fila: confere logo, e
     * ps2_irq_poll decodifica já (antes esperava 256 conferências, ~0,8 ms) */
    if (na_espera(cpu, salvo_epc) && ps2_som_pendente()) g_ps2_contagem_irq = 1;
#ifdef _EE
    { extern unsigned long long g_ps2_exc_ciclos; extern uint32_t g_ps2_exc_ini; uint32_t c;
      __asm__ volatile("mfc0 %0, $9" : "=r"(c)); g_ps2_exc_ciclos += (uint32_t)(c - g_ps2_exc_ini);
      g_prof_irq_ciclos[exc_origem] += (uint32_t)(c - g_ps2_exc_ini); }
#endif
}

static void sair_da_excecao(CPUState *cpu)
{
    uint32_t b = custom_exit;
    int i;
    if (b == 0u) {                                  /* saída padrão */
        return_from_exception(cpu);
        return;
    }
    ps2_diag(16u, ps2_ram_get32(b + 0));
    /* longjmp da BIOS do PS1: ra, sp, fp, s0-s7, gp; devolve 1 em v0. */
    cpu->pc       = ps2_ram_get32(b + 0);
    cpu->gpr[29]  = ps2_ram_get32(b + 4);
    cpu->gpr[30]  = ps2_ram_get32(b + 8);
    for (i = 0; i < 8; i++)
        cpu->gpr[16 + i] = ps2_ram_get32(b + 12 + 4u * (uint32_t)i);
    cpu->gpr[28]  = ps2_ram_get32(b + 44);
    cpu->gpr[2]   = 1;
}

static void entrar_na_excecao(CPUState *cpu, uint32_t pc)
{
    ps2_diag(5u, ps2_irq_pending());
    pmc_antes_exc = pmc_trocar(PMC_IRQ);
#ifdef PS2_TRACE_FMV
    if (ps2_irq_pending() & 8u) TR("exc dma pend %03x pc %08x", ps2_irq_pending(), pc);
#endif
    memcpy(salvo_gpr, cpu->gpr, sizeof(salvo_gpr));
    salvo_hi = cpu->hi; salvo_lo = cpu->lo;
    salvo_epc = pc;
    salvo_sr = cpu->cop0[12];
    /* Como o hardware: empilha os bits de modo/IE (desliga interrupções). */
    cpu->cop0[12] = (salvo_sr & ~0x3Fu) | ((salvo_sr << 2) & 0x3Cu);
    cpu->cop0[14] = pc;               /* EPC */
    cpu->cop0[13] = 0x400u;           /* CAUSE: interrupção (IP2) */
    em_excecao = 1;
    g_entregas++;
#ifdef _EE
    { extern uint32_t g_ps2_exc_ini; __asm__ volatile("mfc0 %0, $9" : "=r"(g_ps2_exc_ini)); }
    {
        uint32_t p = ps2_irq_pending();
        exc_origem = 10;                 /* 10 = nenhuma (DeliverEvent etc.) */
        if (p) { exc_origem = 0; while (!(p & 1u)) { p >>= 1; exc_origem++; } }
        if (exc_origem > 10) exc_origem = 10;
        g_prof_irq_n[exc_origem]++;
    }
#endif

    /* ChangeClearRCnt: a BIOS reconhece sozinha as IRQs de timer/VBlank
     * marcadas (t = 0..2 timers, 3 = VBlank). */
    {
        static const unsigned bit_rcnt[4] = { IRQ_TMR0, IRQ_TMR1, IRQ_TMR2, IRQ_VBLANK };
        unsigned t;
        /* cb_fila NÃO é zerada: entregas pendentes são processadas aqui (F3) */
        for (t = 0; t < 4; t++)
            if (ps2_irq_pending() & (1u << bit_rcnt[t])) {
                if (t < 3) g_ps2_tmr_servindo |= 1u << t;     /* V43 */
                if (rcnt_clear[t])
                    ps2_irq_ack(bit_rcnt[t]);
                ev_entregar(0xF2000000u | t, 0x0002u);   /* RCntCNTn, EvSpINT */
            }
    }

    exc_prio = 0; exc_no = 0; exc_etapa = 0; cb_etapa_salva = -1;
    continuar_excecao(cpu);
    longjmp(g_ps2_volta_ao_laco, 1);
}

int ps2_bios_em_excecao(void) { return em_excecao; }

#include "prof.h"
unsigned long long g_prof_irq, g_prof_cd, g_prof_tmr, g_prof_vblank, g_prof_log;
unsigned long g_ps2_vblanks_perdidos;
static void irq_poll_corpo(CPUState *cpu, uint32_t pc, int *entrar);
void ps2_irq_poll(CPUState *cpu, uint32_t pc)
{
    uint32_t t0 = prof_agora();
    int entrar = 0;
    irq_poll_corpo(cpu, pc, &entrar);
    g_prof_irq += (uint32_t)(prof_agora() - t0);
    if (entrar) entrar_na_excecao(cpu, pc);
    /* V47: parado no laço da espera do quadro: decodifica 1 grupo da música
     * (1/18 de setor, ~45 us no PCSX2) e confere as IRQs de novo daqui a 4
     * conferências (no laço, 256 levavam ~0,8 ms): a IRQ que acaba a
     * espera atrasa no máximo esse grupo. */
    if (!em_excecao && na_espera(cpu, pc) && ps2_som_ocioso())
        g_ps2_contagem_irq = 4;
}

static void irq_poll_corpo(CPUState *cpu, uint32_t pc, int *entrar)
{
#ifdef PS2_TRACE_FMV
    if (g_tr_ativo) ps2_tr_pc(em_excecao ? 1u : pc);
#endif
    ps2_pad_tick();
    uint32_t agora = ps2_ps1_cycles();
    uint32_t sr = cpu->cop0[12];

    {
        uint32_t t0 = prof_agora(), t1, t2;
        ps2_cdrom_update(agora);
        t1 = prof_agora();
        ps2_timers_poll();
        ps2_dma_poll();                   /* V48: fim do DMA1 (MDEC -> RAM) */
        t2 = prof_agora();
        g_prof_cd += (uint32_t)(t1 - t0); g_prof_tmr += (uint32_t)(t2 - t1);
    }
    if (!vblank_iniciado) {
        proximo_vblank = agora + CICLOS_VBLANK;
        vblank_iniciado = 1;
    }
    if ((int32_t)(agora - proximo_vblank) >= 0) {
        uint32_t tv = prof_agora();
        int pmc_r = pmc_trocar(PMC_RUNTIME);   /* trabalho do port no VBlank (e o relatório) */
        ps2_gpu_vblank();
        { extern void ps2_gpu_orcamento_vblank(void); ps2_gpu_orcamento_vblank(); }
        ps2_video_vblank();
        ps2_som_vblank();                 /* V47: música XA */
#if PS2_LOG_ETAPAS
        /* V53: marcos do começo no log, gravados na hora */
        if (g_vblanks == 1u || g_vblanks == 60u || g_vblanks == 300u || g_vblanks == 900u) {
            extern int kit_log_gravar(void);
            printf("etapa: vblank %lu\n", (unsigned long)g_vblanks);
            kit_log_gravar();
        }
#endif
        g_prof_vblank += (uint32_t)(prof_agora() - tv);
        pmc_trocar(pmc_r);
        ps2_irq_raise(IRQ_VBLANK);
        ps2_diag(1u, (uint32_t)g_vblanks);
#ifdef PS2_TRACE_FMV
        if (g_tr_ativo && (g_vblanks % 30u) == 0u) {
            TR("vb %lu pc %08x ra %08x exc %d | cd int1 %lu bfrd %lu perdidos %lu | mdec st %lu rd %lu",
               (unsigned long)g_vblanks, cpu->pc, cpu->gpr[31], em_excecao,
               g_tr_int1, g_tr_bfrd, g_tr_perdidos, g_tr_mdec_st, g_tr_mdec_rd);
            ps2_tr_pc_relatorio();
            ps2_tr_anel("vb");
        }
#endif
        proximo_vblank += CICLOS_VBLANK;
        /* atrasou mais de um período (ex.: gravação do log): pula os VBlanks
         * perdidos SEM perder a fase (antes somava o atraso todo e o PS1
         * ficava ~1,5% mais lento que o PS2: 120 VBlanks para 122 reais) */
        while ((int32_t)(agora - proximo_vblank) >= 0) {
            proximo_vblank += CICLOS_VBLANK;
            g_ps2_vblanks_perdidos++;
        }
        if ((++g_vblanks % 300u) == 0u)
            printf("vblank: %lu (entregues ao jogo: %lu)\n", g_vblanks, g_entregas);
    }
    if (em_excecao || pc == 0 || pc == RET_MAGICO)
        return;
    if (custom_exit == 0 && !lista_irq[0] && !lista_irq[1] && !lista_irq[2] && !lista_irq[3])
        return;
    if (!ps2_irq_pending() || (sr & 0x401u) != 0x401u)   /* IEc e IM2 */
        return;
    *entrar = 1;
}

/* ---------------- memory card (V59, cartao_ps1.c) ---------------- */
/* Eventos do cartão: SwCARD e HwCARD; IOE = pronto, TIMOUT = sem cartão. */
#define EV_SWCARD 0xF4000001u
#define EV_HWCARD 0xF0000011u
#define MC_FD_BASE 2u               /* fds da BIOS: 0 e 1 são o terminal */

static void cartao_evento(int ok)
{
    ev_entregar(EV_SWCARD, ok ? 0x0004u : 0x0100u);
    ev_entregar(EV_HWCARD, ok ? 0x0004u : 0x0100u);
}

/* "bu00:NOME" (porta 1) -> NOME; NULL para outro dispositivo ou porta 2 */
static const char *nome_bu(uint32_t a, char *nome, unsigned cap)
{
    unsigned i;
    for (i = 0; i + 1u < cap; i++) {
        nome[i] = (char)ps2_ram_get8(a + i);
        if (!nome[i]) break;
    }
    nome[i] = 0;
    if (strncmp(nome, "bu00:", 5) != 0) return NULL;
    return nome + 5;
}

static int fd_bu(uint32_t fd) { return fd >= MC_FD_BASE && fd < MC_FD_BASE + MC_FDS && mc_modo((int)(fd - MC_FD_BASE)) >= 0; }

static uint8_t mc_buf[MC_BLOCO];

/* read/write de um arquivo do cartão: copia em pedaços pelo mc_buf */
static uint32_t cartao_rw(uint32_t fd, uint32_t end, uint32_t n, int escrever)
{
    int f = (int)(fd - MC_FD_BASE), modo = mc_modo(f);
    uint32_t feito = 0;
    while (feito < n) {
        uint32_t parte = n - feito, i;
        int r;
        if (parte > MC_BLOCO) parte = MC_BLOCO;
        if (escrever) {
            for (i = 0; i < parte; i++) mc_buf[i] = ps2_ram_get8(end + feito + i);
            r = mc_escrever(f, mc_buf, parte);
        } else {
            r = mc_ler(f, mc_buf, parte);
            for (i = 0; r > 0 && i < (uint32_t)r; i++) ps2_ram_set8(end + feito + i, mc_buf[i]);
        }
        if (r < 0) { feito = 0xFFFFFFFFu; break; }     /* erro (mesmo depois de uma parte) */
        if (r == 0) break;                              /* fim do arquivo */
        feito += (uint32_t)r;
        if ((uint32_t)r < parte) break;
    }
    if (modo & MC_FASYNC) cartao_evento(feito != 0xFFFFFFFFu);
    return feito;
}

static unsigned n_log_cartao;
#define LOG_CARTAO(...) do { if (n_log_cartao < 60u) { n_log_cartao++; printf(__VA_ARGS__); } } while (0)

/* ---------------- chamadas da BIOS ---------------- */


int ps2_bios_call(CPUState *cpu, uint32_t endereco)
{
    uint32_t num = cpu->gpr[9] & 0xFFu;
    uint32_t a0 = cpu->gpr[4], a1 = cpu->gpr[5], a2 = cpu->gpr[6];
    unsigned tab;
    const char *nome;
    uint32_t v0 = 0;

    if (endereco == RET_MAGICO && em_excecao) {     /* voltou de um handler */
        ps2_diag(8u, 0);
        ps2_timer_pad_adianta(0);
        continuar_excecao(cpu);
        return 1;
    }

    switch (endereco & 0x1FFFFFFFu) {
    case 0xA0: tab = 0; nome = nome_de(nomes_a0, N(nomes_a0), num); break;
    case 0xB0: tab = 1; nome = nome_de(nomes_b0, N(nomes_b0), num); break;
    case 0xC0: tab = 2; nome = nome_de(nomes_c0, N(nomes_c0), num); break;
    default: return 0;
    }
    if (!visto[tab][num]) {
        visto[tab][num] = 1;
        printf("bios: %c0(%02lXh) %-28s a0=%08lx a1=%08lx ra=%08lx\n",
               "ABC"[tab], (unsigned long)num, nome, (unsigned long)a0,
               (unsigned long)a1, (unsigned long)cpu->gpr[31]);
    }

    cpu->pc = cpu->gpr[31];     /* padrão: volta para quem chamou (jr $ra) */

    if (tab == 0 && num >= 0x48u && num <= 0x4Cu) {   /* GPU (F1) */
        switch (num) {
        case 0x48: ps2_gpu_gp1(a0); break;                 /* SendGP1Command */
        case 0x49: ps2_gpu_gp0(a0); break;                 /* GPU_cw */
        case 0x4A: {                                       /* GPU_cwp(src, n) */
            uint32_t i;
            for (i = 0; i < a1; i++) ps2_gpu_gp0(ps2_ram_get32(a0 + 4u * i));
            break; }
        case 0x4B: {                                       /* send_gpu_linked_list */
            uint32_t a = a0 & 0x1FFFFCu, nos = 0;
            while (nos++ < 0x80000u) {
                uint32_t cab = ps2_ram_get32(a), n = cab >> 24, i;
                for (i = 0; i < n; i++) ps2_gpu_gp0(ps2_ram_get32(a + 4u + 4u * i));
                if (cab & 0x800000u) break;
                a = cab & 0x1FFFFCu;
            }
            break; }
        case 0x4C: break;                                  /* gpu_abort_dma */
        }
        v0 = 0;
    }
    if (tab == 0 && (num == 0xABu || num == 0xACu)) {   /* _card_info/_card_load */
        int ok = a0 == 0u && g_mc_presente;             /* só a porta 1 tem cartão */
        LOG_CARTAO("cartao: %s(%02lx) -> %s\n", num == 0xABu ? "_card_info" : "_card_load",
                   (unsigned long)a0, ok ? "pronto" : "sem cartao");
        cartao_evento(ok);
        v0 = 1;
    }
    if (tab == 1) {
        switch (num) {
        case 0x17:              /* ReturnFromException */
            if (em_excecao) { return_from_exception(cpu); return 1; }
            break;
        case 0x07:                                                /* DeliverEvent */
            ev_entregar(a0, a1);
            if (!em_excecao && cb_n > 0u) {
                /* Fora de exceção: roda os callbacks agora, como chamadas
                 * do jogo, e depois volta para quem chamou com v0 = 1 (F3). */
                memcpy(salvo_gpr, cpu->gpr, sizeof(salvo_gpr));
                salvo_gpr[2] = 1;
                salvo_hi = cpu->hi; salvo_lo = cpu->lo;
                salvo_sr = cpu->cop0[12];
                salvo_epc = cpu->gpr[31];
                em_excecao = 1; so_callbacks = 1;
                exc_etapa = 0; cb_etapa_salva = -1;
                continuar_excecao(cpu);
                return 1;
            }
            v0 = 1;
            break;
        case 0x08: v0 = ev_abrir(a0, a1, a2, cpu->gpr[7]); break; /* OpenEvent */
        case 0x09: { Evento *e = ev_de(a0); if (e) e->estado = EV_LIVRE; v0 = 1; break; }
        case 0x0A: { Evento *e = ev_de(a0);                       /* WaitEvent */
                     if (e && e->estado == EV_PRONTO) { e->estado = EV_ATIVO; v0 = 1; }
                     else if (e && e->estado == EV_ATIVO) {
                         /* Ainda não aconteceu: repete a chamada (pc = 0xB0,
                          * mesmos registradores) atendendo interrupções (F4). */
                         cpu->pc = endereco;
                         ps2_irq_poll(cpu, endereco);   /* pode fazer longjmp */
                         return 1;
                     }
                     break; }
        case 0x0B: { Evento *e = ev_de(a0);                       /* TestEvent */
                     if (e && e->estado == EV_PRONTO) { e->estado = EV_ATIVO; v0 = 1; }
                     break; }
        case 0x0C: { Evento *e = ev_de(a0);                       /* EnableEvent */
                     if (e && e->estado == EV_FECHADO) e->estado = EV_ATIVO;
                     v0 = 1; break; }
        case 0x0D: { Evento *e = ev_de(a0);                       /* DisableEvent */
                     if (e) e->estado = EV_FECHADO;
                     v0 = 1; break; }
        case 0x20: { unsigned i;                                  /* UnDeliverEvent */
                     for (i = 0; i < EV_MAX; i++)
                         if (ev[i].estado == EV_PRONTO && ev[i].classe == a0 &&
                             ev[i].spec == a1) ev[i].estado = EV_ATIVO;
                     break; }
        case 0x18: custom_exit = 0; break;          /* SetDefaultExitFromException */
        case 0x19: custom_exit = a0; break;         /* SetCustomExitFromException */
        case 0x32: {                                /* open(nome, modo) */
            char nome[40];
            const char *n = nome_bu(a0, nome, sizeof(nome));
            int fd = n ? mc_abrir(n, a1) : -1;
            v0 = fd >= 0 ? (uint32_t)fd + MC_FD_BASE : 0xFFFFFFFFu;
            LOG_CARTAO("cartao: open(\"%s\", %lx) -> %ld\n", nome, (unsigned long)a1, (long)(int32_t)v0);
            break; }
        case 0x33:                                  /* lseek(fd, pos, de) */
            v0 = fd_bu(a0) ? (uint32_t)mc_posicionar((int)(a0 - MC_FD_BASE), (int32_t)a1, (int)a2) : 0xFFFFFFFFu;
            break;
        case 0x34:                                  /* read(fd, dst, n) */
            v0 = fd_bu(a0) ? cartao_rw(a0, a1, a2, 0) : 0xFFFFFFFFu;
            LOG_CARTAO("cartao: read(%lu, %lu) -> %ld\n", (unsigned long)a0, (unsigned long)a2, (long)(int32_t)v0);
            break;
        case 0x36: {                                /* close(fd) */
            int r = fd_bu(a0) ? mc_fechar((int)(a0 - MC_FD_BASE)) : -1;
            v0 = r >= 0 ? (uint32_t)r + MC_FD_BASE : 0xFFFFFFFFu;
            break; }
        case 0x41:                                  /* format("bu00:") */
            { char nome[40]; v0 = 0;
              if (nome_bu(a0, nome, sizeof(nome)) && g_mc_presente) { mc_formatar_cartao(); v0 = 1; }
              LOG_CARTAO("cartao: format(\"%s\") -> %lu\n", nome, (unsigned long)v0); }
            break;
        case 0x42: case 0x43: {                     /* firstfile(padrao, ent) / nextfile(ent) */
            uint8_t ent[0x28]; char nome[40]; unsigned i; int achou = 0;
            uint32_t dest = num == 0x42u ? a1 : a0;
            if (num == 0x42u) { const char *n = nome_bu(a0, nome, sizeof(nome)); achou = n && mc_primeiro(n, ent); }
            else achou = mc_proximo(ent);
            if (achou) for (i = 0; i < sizeof(ent); i++) ps2_ram_set8(dest + i, ent[i]);
            v0 = achou ? dest : 0;
            LOG_CARTAO("cartao: %s -> %.20s\n", num == 0x42u ? "firstfile" : "nextfile", achou ? (char *)ent : "(nada)");
            break; }
        case 0x45: {                                /* erase(nome) */
            char nome[40]; const char *n = nome_bu(a0, nome, sizeof(nome));
            v0 = n ? (uint32_t)mc_apagar(n) : 0;
            LOG_CARTAO("cartao: erase(\"%s\") -> %lu\n", nome, (unsigned long)v0);
            break; }
        case 0x4E: case 0x4F: {                     /* write/read_card_sector(porta, quadro, buf) */
            uint8_t q[MC_QUADRO]; unsigned i; int ok = a0 == 0u && g_mc_presente;
            if (ok && num == 0x4Eu) {
                for (i = 0; i < MC_QUADRO; i++) q[i] = ps2_ram_get8(a2 + i);
                ok = mc_quadro_escrever(a1, q);
            } else if (ok) {
                ok = mc_quadro_ler(a1, q);
                for (i = 0; ok && i < MC_QUADRO; i++) ps2_ram_set8(a2 + i, q[i]);
            }
            LOG_CARTAO("cartao: %s(%02lx, %lu) -> %s\n", num == 0x4Eu ? "write_card_sector" : "read_card_sector",
                       (unsigned long)a0, (unsigned long)a1, ok ? "ok" : "sem cartao");
            cartao_evento(ok);
            v0 = 1;
            break; }
        case 0x35:                                  /* write(fd, buf, len) */
            if (fd_bu(a0)) {
                v0 = cartao_rw(a0, a1, a2, 1);
                LOG_CARTAO("cartao: write(%lu, %lu) -> %ld\n", (unsigned long)a0, (unsigned long)a2, (long)(int32_t)v0);
                break;
            }
            if (a0 == 1u) {
                uint32_t i;
                for (i = 0; i < a2 && i < 512u; i++)
                    putchar(ps2_ram_get8(a1 + i));
            }
            v0 = a2;
            break;
        case 0x3D: putchar((int)(a0 & 0xFFu)); v0 = 1; break;   /* putchar */
        }
    } else if (tab == 2) {
        switch (num) {
        case 0x02:              /* SysEnqIntRP(prio, struct): insere no começo */
            if (a0 < 4u) {
                ps2_ram_set32(a1, lista_irq[a0]);
                lista_irq[a0] = a1;
            }
            break;
        case 0x03:              /* SysDeqIntRP(prio, struct) */
            if (a0 < 4u) {
                uint32_t *p = &lista_irq[a0], n = *p, ant = 0;
                while (n) {
                    if (n == a1) {
                        if (ant) ps2_ram_set32(ant, ps2_ram_get32(n));
                        else *p = ps2_ram_get32(n);
                        break;
                    }
                    ant = n; n = ps2_ram_get32(n);
                }
            }
            break;
        case 0x0A:              /* ChangeClearRCnt(t, flag): devolve o antigo */
            if (a0 < 4u) { v0 = rcnt_clear[a0]; rcnt_clear[a0] = a1; }
            break;
        }
    }
    cpu->gpr[2] = v0;
    return 1;
}
