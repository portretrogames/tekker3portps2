/*
 * nativo_overlay.h — V48: tabela das rotinas de overlay traduzidas para C
 * (tools/nativo.py com tools/nativas_overlay.txt, em build/r/nativo.c).
 * Usada pelo interpretador (interp.c) e pelo C gerado, para os dois lados
 * terem sempre a mesma estrutura.
 */
#ifndef PS2_NATIVO_OVERLAY_H
#define PS2_NATIVO_OVERLAY_H

#include <stdint.h>
#include "cpu_state.h"

typedef struct {
    uint32_t ini, n;              /* endereço de entrada e nº de palavras */
    const uint32_t *cod;          /* código esperado na RAM (de ini a fim) */
    void (*nat)(CPUState *);      /* versão nativa */
} Ps2NatOverlay;

extern const Ps2NatOverlay ps2_nat_overlays[];
extern const unsigned ps2_nat_n_overlays;
int ps2_nat_executar_overlay(CPUState *cpu, uint32_t ini, void (*nat)(CPUState *));

#endif
