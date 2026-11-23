/* pmc.c — contadores de desempenho do R5900 por região (ver pmc.h). */
#include "pmc.h"
#if defined(_EE) && defined(PS2_PMC)
#include <stdio.h>
#include <string.h>

#ifdef PS2_PMC_VARRE
unsigned g_ps2_pilha_extra;
#endif
uint64_t g_pmc_soma[PMC_REGIOES][3];
uint32_t g_pmc_ult[3];
int g_pmc_regiao;

void ps2_pmc_zerar_contadores(void)
{
    __asm__ volatile("mtpc $0, 0\n\tmtpc $0, 1\n\tsync.p");
    g_pmc_ult[0] = g_pmc_ult[1] = 0;
}

void ps2_pmc_relatorio(void)
{
    /* pares (evento do PCR0, evento do PCR1), manual do EE Core */
    static const struct { uint8_t e0, e1; const char *n0, *n1; } cfg[] = {
        { 6, 6,  "faltas do cache de instrucoes", "faltas do cache de dados" },
        { 12, 3, "instrucoes completadas",        "desvios errados" },
        { 15, 15, "loads",                        "stores" },
        { 4, 4,  "faltas do BTAC",                "faltas do TLB" },
    };
    static const char *const nome[] = {
        [PMC_JOGO] = "jogo", [PMC_NATIVAS] = "nativas", [PMC_LISTA] = "lista",
        [PMC_ESPERA] = "espera", [PMC_IRQ] = "irq", [PMC_RUNTIME] = "runtime",
    };
    _Static_assert(sizeof(nome) / sizeof(nome[0]) == PMC_REGIOES, "nome de cada regiao do pmc.h");
    static int atual = -1;
    unsigned r;
    if (atual >= 0) {
        pmc_trocar(g_pmc_regiao);           /* fecha a região em andamento */
        printf("pmc: [%s | %s] (eventos em milhares, ciclos em milhoes)", cfg[atual].n0, cfg[atual].n1);
        for (r = 0; r < PMC_REGIOES; r++)
            printf(" | %s %lu/%lu em %lu", nome[r], (unsigned long)(g_pmc_soma[r][0] / 1000u),
                   (unsigned long)(g_pmc_soma[r][1] / 1000u), (unsigned long)(g_pmc_soma[r][2] / 1000000u));
#ifdef PS2_PMC_VARRE
        printf(" | pilha %u", g_ps2_pilha_extra);
#endif
        printf("\n");
    }
#ifdef PS2_PMC_VARRE
    {   static unsigned n_rel;
        atual = 0;                          /* sempre as faltas dos dois caches */
        g_ps2_pilha_extra = (n_rel++ % 8u) * 512u; }
#else
    atual = (atual + 1) % (int)(sizeof(cfg) / sizeof(cfg[0]));
#endif
    memset(g_pmc_soma, 0, sizeof(g_pmc_soma));
    {
        /* CTE | EVENT1 | U1 S1 K1 EXL1 | EVENT0 | U0 S0 K0 EXL0 */
        uint32_t pccr = 0x80000000u | ((uint32_t)cfg[atual].e1 << 15) | 0x7800u |
                        ((uint32_t)cfg[atual].e0 << 5) | 0x1Eu, c;
        __asm__ volatile("mtps $0, 0\n\tsync.p");
        ps2_pmc_zerar_contadores();
        __asm__ volatile("mtps %0, 0\n\tsync.p" : : "r"(pccr));
        __asm__ volatile("mfc0 %0, $9" : "=r"(c));
        g_pmc_ult[2] = c;
    }
}
#else
typedef int pmc_vazio;      /* sem PMC=1 este arquivo não gera nada */
#endif
