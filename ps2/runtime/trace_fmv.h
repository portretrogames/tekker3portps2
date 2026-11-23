/*
 * trace_fmv.h — rastreio do vídeo da abertura (MDEC, DMA0/1, CD), ver
 * trace_fmv.c. Só entra com -DPS2_TRACE_FMV (make TRACE_FMV=1); sem ele os
 * ganchos TR(...) somem.
 */
#ifndef PS2_TRACE_FMV_H
#define PS2_TRACE_FMV_H

#ifdef PS2_TRACE_FMV
#include <stdint.h>
extern int g_tr_ativo;
extern unsigned long g_tr_int1, g_tr_bfrd, g_tr_perdidos, g_tr_mdec_st, g_tr_mdec_rd;
void ps2_tr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ps2_tr_pc(uint32_t pc);           /* toda conferência de IRQ */
void ps2_tr_pci(uint32_t pc);          /* conferências no interpretador */
void ps2_tr_pc_relatorio(void);        /* a cada 30 VBlanks */
void ps2_tr_anel(const char *rot);     /* anel do streaming do libcd */
#define TR(...) do { if (g_tr_ativo) ps2_tr(__VA_ARGS__); } while (0)
#define TR_LIGAR() (g_tr_ativo = 1)
#define TR_CONTA(v) ((v)++)
#else
#define TR(...) do { } while (0)
#define TR_LIGAR() do { } while (0)
#define TR_CONTA(v) do { } while (0)
#endif

#endif
