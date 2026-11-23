/*
 * trace_fmv.c — rastreio do vídeo da abertura (V48), só com make TRACE_FMV=1
 * (-DPS2_TRACE_FMV). Liga no 1º acesso ao MDEC e escreve no log linhas
 * "fmv <milhares de ciclos do PS1>: ...": comandos e DMAs do MDEC, DICR, IRQs
 * de DMA, cada setor do CD com o estado do anel do libcd e, a cada 30
 * VBlanks, onde o código está parado (histogramas do pc). Sem o define, o
 * arquivo fica vazio. O despejo da entrada do MDEC (mdec.c) é separado:
 * -DPS2_DUMP_MDEC. Foi assim que o travamento da abertura foi achado
 * (ANALISE_V42_V43.md, V48).
 */
#ifdef PS2_TRACE_FMV
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "trace_fmv.h"
#include "io.h"
#include "ram.h"

int g_tr_ativo;
unsigned long g_tr_int1, g_tr_bfrd, g_tr_perdidos, g_tr_mdec_st, g_tr_mdec_rd;
static unsigned tr_linhas;

void ps2_tr(const char *fmt, ...)
{
    va_list ap;
    if (tr_linhas >= 6000u) return;              /* não enche o log */
    tr_linhas++;
    printf("fmv %7lu: ", (unsigned long)(ps2_ps1_cycles() / 1000u));
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

/* histograma pequeno de endereços (tabela aberta de 64 posições) */
#define TR_PCS 64u
typedef struct { uint32_t pc[TR_PCS], n[TR_PCS]; unsigned long total; } Histo;
static Histo h_poll, h_interp;

static void histo_somar(Histo *h, uint32_t pc)
{
    unsigned k = (pc >> 2) % TR_PCS, i;
    h->total++;
    for (i = 0; i < TR_PCS; i++, k = (k + 1u) % TR_PCS) {
        if (h->n[k] && h->pc[k] == pc) { h->n[k]++; return; }
        if (!h->n[k]) { h->pc[k] = pc; h->n[k] = 1; return; }
    }
}

static void histo_relatar(Histo *h, const char *rot, unsigned quantos)
{
    char buf[320];
    int n = 0;
    unsigned i, k;
    for (k = 0; k < quantos; k++) {
        unsigned m = 0;
        int w;
        for (i = 1; i < TR_PCS; i++) if (h->n[i] > h->n[m]) m = i;
        if (!h->n[m]) break;
        w = snprintf(buf + n, sizeof(buf) - (size_t)n, " %08lx:%lu",
                     (unsigned long)h->pc[m], (unsigned long)h->n[m]);
        if (w < 0) break;
        if ((size_t)w >= sizeof(buf) - (size_t)n) { n = (int)sizeof(buf) - 1; break; }
        n += w;
        h->n[m] = 0;
    }
    buf[n] = 0;
    TR("%s (%lu):%s", rot, h->total, buf);
    memset(h, 0, sizeof(*h));
}

/* toda conferência de IRQ (pc 1 = dentro de exceção) */
void ps2_tr_pc(uint32_t pc) { histo_somar(&h_poll, pc); }
/* conferências dentro do interpretador (a cada 256 instruções; blocos de 16 bytes) */
void ps2_tr_pci(uint32_t pc) { histo_somar(&h_interp, pc & ~15u); }

void ps2_tr_pc_relatorio(void)
{
    histo_relatar(&h_interp, "interp, conferencias", 10u);
    histo_relatar(&h_poll, "pcs", 6u);
}

/* Estado do anel do streaming do libcd do Tekken (StSetRing 80082B08,
 * StCdInterrupt 80084DC8, StGetNext 80084CE8, StFreeRing 80084BF8): são
 * variáveis do jogo, só para o log. Uma letra por posição do anel: 0 livre,
 * 1 marca de volta ao início, 2 quadro completo, 3 setor gravado, 4 quadro em
 * uso pelo tocador. esc/ini/lei = posição de escrita, início do quadro sendo
 * gravado e posição de leitura; esp = próximo setor esperado do quadro. */
void ps2_tr_anel(const char *rot)
{
    uint32_t base = ps2_ram_get32(0x0b0418u) & 0x1ffffcu;
    uint32_t n = ps2_ram_get32(0x0b0538u), i;
    char st[72];
    if (n > 64u) n = 64u;
    for (i = 0; i < n; i++) {
        unsigned v = ps2_ram_get32((base + 32u * i) & 0x1ffffcu) & 0xFFFFu;
        st[i] = (char)(v < 10u ? '0' + v : v < 36u ? 'a' + v - 10u : '?');
    }
    st[n] = 0;
    TR("anel %s: cod %lu esc %lu ini %lu lei %lu | esp %lu quadro %lu df38 %lu | %s", rot,
       (unsigned long)ps2_ram_get32(0x099b10u), (unsigned long)ps2_ram_get32(0x0afa44u),
       (unsigned long)ps2_ram_get32(0x0afa48u), (unsigned long)ps2_ram_get32(0x0afa54u),
       (unsigned long)(ps2_ram_get32(0x0a8ce4u) & 0xFFFFu), (unsigned long)ps2_ram_get32(0x0a8678u),
       (unsigned long)ps2_ram_get32(0x0adf38u), st);
}
#else
typedef int ps2_trace_fmv_vazio;   /* ISO C não aceita unidade de tradução vazia */
#endif
