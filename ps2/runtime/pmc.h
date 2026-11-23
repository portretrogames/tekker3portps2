/*
 * pmc.h — contadores de desempenho do R5900 (PCCR, PCR0, PCR1), por região.
 * Só com "make PMC=1" e só no console (o PCSX2 não conta eventos de cache).
 * Não combina com NAT_VERIFICAR=1 (a verificação roda fora das regiões).
 *
 * Responde "o tempo vai em quê": instruções executadas, faltas do cache de
 * instruções (16 KB) e de dados (8 KB), desvios errados, loads/stores. A cada
 * relatório (~2 s) troca o par de eventos; a linha "pmc:" sai por região:
 *   jogo    código recompilado do jogo (lógica + montagem do desenho)
 *   nativas rotinas em C nativo (tools/nativo.py)
 *   lista   conversão das listas de desenho para o GS (io.c/gpu.c/gs_render.c)
 *   espera  laço da espera do quadro (ocioso)
 *   irq     tratadores de interrupção do jogo (VBlank, timer 2), fora a lista
 *   runtime trabalho do port no VBlank (exibição, música, relatório, log)
 */
#ifndef PS2_PMC_H
#define PS2_PMC_H
#include <stdint.h>

enum { PMC_JOGO, PMC_NATIVAS, PMC_LISTA, PMC_ESPERA, PMC_IRQ, PMC_RUNTIME, PMC_REGIOES };

#if defined(_EE) && defined(PS2_PMC)
#ifdef PS2_NAT_VERIFICAR
#error "PMC=1 e NAT_VERIFICAR=1 nao combinam (a verificacao roda fora das regioes)"
#endif
extern uint64_t g_pmc_soma[PMC_REGIOES][3];   /* evento 0, evento 1, ciclos */
extern uint32_t g_pmc_ult[3];
extern int g_pmc_regiao;
void ps2_pmc_zerar_contadores(void);

/* acumula na região atual e passa para outra; devolve a anterior.
 * (as três leituras são asm volatile: o compilador não troca a ordem entre
 * elas; a diferença de poucos ciclos entre uma e outra não pesa na soma) */
static inline int pmc_trocar(int nova)
{
    uint32_t a, b, c;
    int ant = g_pmc_regiao;
    __asm__ volatile("mfpc %0, 0" : "=r"(a));
    __asm__ volatile("mfpc %0, 1" : "=r"(b));
    __asm__ volatile("mfc0 %0, $9" : "=r"(c));
    g_pmc_soma[ant][0] += (uint32_t)(a - g_pmc_ult[0]);
    g_pmc_soma[ant][1] += (uint32_t)(b - g_pmc_ult[1]);
    g_pmc_soma[ant][2] += (uint32_t)(c - g_pmc_ult[2]);
    g_pmc_ult[0] = a; g_pmc_ult[1] = b; g_pmc_ult[2] = c;
    /* o bit 31 de um contador gera exceção: zera bem antes (um relatório que
     * atrase muito, ex.: carregamento longo, não derruba o jogo) */
    if (__builtin_expect((a | b) & 0x40000000u, 0)) ps2_pmc_zerar_contadores();
    g_pmc_regiao = nova;
    return ant;
}
void ps2_pmc_relatorio(void);
#else
static inline int pmc_trocar(int nova) { (void)nova; return 0; }
static inline void ps2_pmc_relatorio(void) {}
#endif

/*
 * make PMC=1 PMC_VARRE=1 (ou 2): varredura da posição da pilha do EE no cache
 * de dados (quente.h). O par de eventos fica fixo (faltas dos dois caches) e
 * a cada relatório o alvo da pilha anda 512 bytes (8 conjuntos do cache; 8
 * posições = os 4 KB):
 *   PMC_VARRE=1  a pilha das rotinas nativas e da lista de desenho
 *   PMC_VARRE=2  a pilha do código do jogo (laço de despacho de main.c)
 * A linha "pmc:" traz "pilha N". As faltas e os ciclos de cada posição dizem
 * onde a pilha disputa conjunto com os dados fixos: tools/varre_pilha.py.
 */
#if defined(_EE) && defined(PS2_PMC) && defined(PS2_PMC_VARRE)
extern unsigned g_ps2_pilha_extra;
#define PMC_PILHA_DESENHO(padrao) (PS2_PMC_VARRE == 1 ? g_ps2_pilha_extra : (padrao))
#define PMC_PILHA_JOGO(padrao)    (PS2_PMC_VARRE == 2 ? g_ps2_pilha_extra : (padrao))
#define PMC_VARRE_JOGO (PS2_PMC_VARRE == 2)
#else
#define PMC_PILHA_DESENHO(padrao) (padrao)
#define PMC_PILHA_JOGO(padrao)    (padrao)
#define PMC_VARRE_JOGO 0
#endif
#endif
