/*
 * prof.h — perfilador simples para medir no PS2 real (vai para o log).
 * Ciclos do EE (COUNT) por área e contagem de acessos lentos à memória.
 */
#ifndef PS2_PROF_H
#define PS2_PROF_H
#include <stdint.h>

extern unsigned long long g_prof_gte, g_prof_irq, g_prof_interp;
extern unsigned long g_prof_gte_n, g_prof_scratch_n, g_prof_io_n;
/* V46: acessos à RAM pelo caminho lento de memory.c (só KUSEG/KSEG1 deveriam) */
extern unsigned long g_prof_ram_lenta;
/* V45: GTE por comando (índice = cmd & 0x3F); só contado no ELF com log */
extern unsigned long g_prof_gte_cmd_n[64];
extern unsigned long long g_prof_gte_cmd_c[64];
extern unsigned g_prof_hist[2048];          /* amostras por bloco de 1 KB da RAM do PS1 */
extern unsigned g_prof_amostras, g_prof_amostras_exc;

static inline uint32_t prof_agora(void)
{
#ifdef _EE
    uint32_t c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c;
#else
    return 0;
#endif
}
#endif
