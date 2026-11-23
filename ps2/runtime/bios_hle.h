/*
 * bios_hle.h — BIOS do PS1 em alto nível (HLE).
 *
 * Os jogos de PS1 chamam a BIOS pulando para 0xA0, 0xB0 ou 0xC0 com o
 * número da função em t1 ($9). Aqui essas chamadas são atendidas em C.
 * Fase 2: por enquanto só registra cada função usada (1ª vez) e devolve 0,
 * para levantar a lista de funções que o Tekken 3 precisa.
 */
#ifndef PS2_BIOS_HLE_H
#define PS2_BIOS_HLE_H

#include "cpu_state.h"

#include <setjmp.h>

/* 1 se tratou a chamada (cpu->pc já aponta para o retorno). */
int ps2_bios_call(CPUState *cpu, uint32_t endereco);

/*
 * Interrupções: gera o VBlank (60 Hz) e, se houver interrupção pendente e
 * habilitada, entra na "exceção": salva os registradores, pula para o
 * handler que o jogo registrou (SetCustomExitFromException) e sai do código
 * gerado com longjmp para o laço de despacho (main.c). O jogo termina o
 * handler chamando ReturnFromException, que restaura tudo e volta para pc.
 */
extern jmp_buf g_ps2_volta_ao_laco;
void ps2_irq_poll(CPUState *cpu, uint32_t pc);

#endif
