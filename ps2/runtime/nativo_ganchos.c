/*
 * nativo_ganchos.c — liga as rotinas traduzidas por tools/nativo.py
 * (build/r/nativo.c) no lugar do código do psxrecomp, e o ajuste do laço da
 * luta (tools/transformar_gerado.py).
 *
 * O código gerado chama ps2_nat_XXXXXXXX_gancho() no começo da função (só na
 * entrada normal, cpu->pc == 0).
 */
#include <stdint.h>
#include "cpu_state.h"
#include "ram.h"
#include "nativo_overlay.h"
#include "quente.h"

static int executar(CPUState *cpu, void (*nat)(CPUState *))
{
    PS2_PILHA_EM(PS2_PILHA_NATIVAS);    /* quente.h: posição fixa no cache de dados */
    nat(cpu);
    return 1;
}

/* Chamado pelos ganchos gerados em build/r/nativo.c. */
int ps2_nat_executar(CPUState *cpu, uint32_t ini, void (*nat)(CPUState *))
{
    (void)ini;
    return executar(cpu, nat);
}

/* V48: rotina de overlay (runtime/interp.c). */
int ps2_nat_executar_overlay(CPUState *cpu, uint32_t ini, void (*nat)(CPUState *))
{
    (void)ini;
    return executar(cpu, nat);
}

/* V55: valor devolvido pela lógica 8002AE58 no laço 800508C4 (s0): 1 = luta,
 * 0 = apresentação do round (ROUND 1 / READY? / FIGHT), 2/4/6 = fim do round.
 * Só com 1 o jogo roda o passo de recuperação sem desenho (no PS1 a
 * apresentação cabia em 1 VBlank e não precisava). No PS2 a apresentação
 * andava na metade da velocidade, com as falas junto; a recuperação vale
 * também para o 0. Devolve 1 = sem recuperação. */
int ps2_luta_sem_recup(CPUState *cpu)
{
    uint32_t v = cpu->gpr[16];          /* s0 = o que 8002AE58 devolveu */
    if (v == 1u) return 0;
    /* o jogo grava s0 na marca 800ADCA4 ("passo sem desenho"): com 0 a marca
     * não ligava e o laço repetia passos com desenho até o jogo cair; vira 1
     * (a chamada seguinte de 8002AE58 escreve s0 de novo) */
    if (v == 0u) { cpu->gpr[16] = 1u; return 0; }
    return 1;
}
