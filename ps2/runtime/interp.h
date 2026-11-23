/*
 * interp.h — interpretador MIPS R3000A para código sem compilação estática.
 *
 * Usado para os overlays que o Tekken 3 carrega do CD durante o jogo.
 * Executa a partir de cpu->pc até um salto cair em código compilado (ou na
 * BIOS: 0xA0/0xB0/0xC0), quando devolve o controle ao laço de despacho.
 */
#ifndef PS2_INTERP_H
#define PS2_INTERP_H

#include "cpu_state.h"

/* Executa até sair para código compilado/BIOS. Devolve 0 se o endereço
 * inicial não é RAM executável (aí o laço para com erro). */
int ps2_interp_run(CPUState *cpu);


/* Endereço que o código compilado pediu para interpretar (stubs.c). */
extern uint32_t g_ps2_interpretar_em;   /* contador (diagnóstico) */

#endif
