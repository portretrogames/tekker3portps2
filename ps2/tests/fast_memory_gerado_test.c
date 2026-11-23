/*
 * tests/fast_memory_gerado_test.c — V46: só compila (make test usa
 * -fsyntax-only), com as mesmas opções do código gerado
 * (-DPS2_GENERATED_FAST_MEMORY=1 -include runtime/fast_memory.h).
 *
 * Confere que, no código gerado, LW/LH/LB (psx_cyc_load_word/half/byte) e
 * as leituras diretas (psx_read_word/half) usam o caminho rápido de
 * fast_memory.h. Até a V45 o psx_cyc.h do psxrecomp entrava antes dos
 * #define e todo LW/LH chamava memory.c sem ninguém perceber. Se os #define
 * sumirem, o #error para a compilação; se forem para antes de cpu_state.h, o
 * psx_cyc.h passa a redefinir ps2_fast_cyc_load_* e a compilação também
 * falha.
 */
#include "cpu_state.h"
#include "psx_cyc.h"

#if !defined(psx_cyc_load_word) || !defined(psx_cyc_load_half) || !defined(psx_cyc_load_byte)
#error "fast_memory.h: LW/LH/LB do codigo gerado nao vao para o caminho rapido"
#endif
#if !defined(psx_read_word) || !defined(psx_read_half)
#error "fast_memory.h: leituras diretas do codigo gerado nao vao para o caminho rapido"
#endif

uint32_t teste_lw(CPUState *cpu, uint32_t a) { return psx_cyc_load_word(cpu, a, 2u, 0u); }
uint16_t teste_lh(CPUState *cpu, uint32_t a) { return psx_cyc_load_half(cpu, a, 2u, 0u); }
uint8_t  teste_lb(CPUState *cpu, uint32_t a) { return psx_cyc_load_byte(cpu, a, 2u, 0u); }
