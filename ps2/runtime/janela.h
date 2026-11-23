/*
 * janela.h — acesso direto à RAM e ao scratchpad do PS1 pela janela única
 * (ram.h), sem testar a região: endereço & 0xFFFFFF, uma instrução de
 * memória alinhada (lw/sw/lh/sh/lb/sb).
 *
 * Só para código que comprovadamente só toca RAM (0x80000000-0x801FFFFF) e
 * scratchpad (0x1F800000-0x1F8003FF), com acessos alinhados como o PS1
 * exige: hoje as rotinas nativas de tools/nativo.py. Fora disso (E/S, BIOS,
 * espelhos da RAM, KUSEG/KSEG1) o resultado é lixo, sem aviso.
 *
 * Por quê (medido no console com PMC=1): com os dois testes e a chamada
 * lenta em linha, ps2_nat_80036CAC tinha 42,7 KB para um cache de instruções
 * de 16 KB, e as nativas levavam 1,3 M de faltas de cache por janela de 2 s.
 * As escritas não marcam código sujo (ps2_cod_marcar): estas rotinas só
 * gravam dados (primitivas, OT, scratchpad).
 */
#ifndef PS2_JANELA_H
#define PS2_JANELA_H
#include <stdint.h>
#include "ram.h"

typedef int16_t ps2_s16a __attribute__((may_alias));

#define PS2_JANELA_LE(a, bytes, direto, lento)    (direto)
#define PS2_JANELA_GRAVA(a, bytes, direto, lento) do { direto; } while (0)

#define PS2_JANELA_P(a) (g_ps2_janela + ((a) & PS2_JANELA_MASK))

static inline __attribute__((always_inline)) uint32_t ps2_jan_rd32(uint32_t a)
{
    return PS2_JANELA_LE(a, 4u, *(const ps2_u32a *)PS2_JANELA_P(a), ps2_fast_read_word(a));
}
static inline __attribute__((always_inline)) uint32_t ps2_jan_rd16(uint32_t a)
{
    return PS2_JANELA_LE(a, 2u, *(const ps2_u16a *)PS2_JANELA_P(a), ps2_fast_read_half(a));
}
static inline __attribute__((always_inline)) uint32_t ps2_jan_rd16s(uint32_t a)
{
    return (uint32_t)(int32_t)PS2_JANELA_LE(a, 2u, *(const ps2_s16a *)PS2_JANELA_P(a), (int16_t)ps2_fast_read_half(a));
}
static inline __attribute__((always_inline)) uint32_t ps2_jan_rd8(uint32_t a)
{
    return PS2_JANELA_LE(a, 1u, *(const uint8_t *)PS2_JANELA_P(a), ps2_fast_read_byte(a));
}
static inline __attribute__((always_inline)) uint32_t ps2_jan_rd8s(uint32_t a)
{
    return (uint32_t)(int32_t)PS2_JANELA_LE(a, 1u, *(const int8_t *)PS2_JANELA_P(a), (int8_t)ps2_fast_read_byte(a));
}
static inline __attribute__((always_inline)) void ps2_jan_wr32(uint32_t a, uint32_t v)
{
    PS2_JANELA_GRAVA(a, 4u, *(ps2_u32a *)PS2_JANELA_P(a) = v, ps2_fast_write_word(a, v));
}
static inline __attribute__((always_inline)) void ps2_jan_wr16(uint32_t a, uint16_t v)
{
    PS2_JANELA_GRAVA(a, 2u, *(ps2_u16a *)PS2_JANELA_P(a) = v, ps2_fast_write_half(a, v));
}
static inline __attribute__((always_inline)) void ps2_jan_wr8(uint32_t a, uint8_t v)
{
    PS2_JANELA_GRAVA(a, 1u, *(uint8_t *)PS2_JANELA_P(a) = v, ps2_fast_write_byte(a, v));
}

/*
 * Pilha do PS1 (código gerado, tools/transformar_gerado.py): base = valor do
 * sp, k = deslocamento fixo. O ponteiro da janela é calculado a partir do sp
 * (o gcc reaproveita entre os acessos de uma função) e k entra direto na
 * instrução de memória. (sp & máscara) + k só difere de (sp + k) & máscara se
 * a soma passar do fim da região, o que não existe num quadro de pilha válido.
 */
static inline __attribute__((always_inline)) uint32_t ps2_pil_rd32(uint32_t sp, int32_t k)
{ return *(const ps2_u32a *)(PS2_JANELA_P(sp) + k); }
static inline __attribute__((always_inline)) uint32_t ps2_pil_rd16(uint32_t sp, int32_t k)
{ return *(const ps2_u16a *)(PS2_JANELA_P(sp) + k); }
static inline __attribute__((always_inline)) uint32_t ps2_pil_rd8(uint32_t sp, int32_t k)
{ return *(const uint8_t *)(PS2_JANELA_P(sp) + k); }
static inline __attribute__((always_inline)) void ps2_pil_wr32(uint32_t sp, int32_t k, uint32_t v)
{ *(ps2_u32a *)(PS2_JANELA_P(sp) + k) = v; }
static inline __attribute__((always_inline)) void ps2_pil_wr16(uint32_t sp, int32_t k, uint16_t v)
{ *(ps2_u16a *)(PS2_JANELA_P(sp) + k) = v; }
static inline __attribute__((always_inline)) void ps2_pil_wr8(uint32_t sp, int32_t k, uint8_t v)
{ *(uint8_t *)(PS2_JANELA_P(sp) + k) = v; }
#endif
