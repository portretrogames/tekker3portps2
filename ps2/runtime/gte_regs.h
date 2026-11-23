/*
 * gte_regs.h — leitura/escrita dos registradores de dados do GTE (mfc2/mtc2/
 * lwc2). Em linha: o código gerado passa o número do registrador como
 * constante, então o switch some na compilação (V23; antes cada mtc2/lwc2
 * era uma chamada de função). gte.c usa as mesmas funções.
 */
#ifndef PS2_GTE_REGS_H
#define PS2_GTE_REGS_H
#include <stdint.h>
#include "cpu_state.h"

static inline uint32_t sinal16(uint32_t valor)
{
    valor &= 0xffffu;
    return (valor & 0x8000u) ? valor | 0xffff0000u : valor;
}

static inline uint32_t componente(uint32_t valor)
{
    int32_t n = (int32_t)sinal16(valor);
    if (n <= 0) return 0;
    if (n >= 0xf80) return 31;
    return (uint32_t)n >> 7;
}

static inline uint32_t cor_ir(const CPUState *cpu)
{
    return componente(cpu->gte_data[9]) |
           (componente(cpu->gte_data[10]) << 5) |
           (componente(cpu->gte_data[11]) << 10);
}

static inline uint32_t zeros_iniciais(uint32_t valor)
{
    uint32_t n = 0;
    if (valor & 0x80000000u) valor = ~valor;
    while (n < 32 && !(valor & 0x80000000u)) {
        valor <<= 1;
        ++n;
    }
    return n;
}

static inline __attribute__((always_inline)) uint32_t gte_rd_data_i(CPUState *cpu, uint8_t reg)
{
    switch (reg & 31u) {
    case 1: case 3: case 5: case 8: case 9: case 10: case 11:
        return sinal16(cpu->gte_data[reg & 31u]);
    case 7: case 16: case 17: case 18: case 19:
        return cpu->gte_data[reg & 31u] & 0xffffu;
    case 15: return cpu->gte_data[14];
    case 28: case 29: return cor_ir(cpu);
    case 31: return zeros_iniciais(cpu->gte_data[30]);
    default: return cpu->gte_data[reg & 31u];
    }
}

static inline __attribute__((always_inline)) void gte_wr_data_i(CPUState *cpu, uint8_t reg, uint32_t valor)
{
    uint32_t *d = cpu->gte_data;
    reg &= 31u;
    switch (reg) {
    case 1: case 3: case 5: case 8: case 9: case 10: case 11:
        d[reg] = sinal16(valor);
        break;
    case 7: case 16: case 17: case 18: case 19:
        d[reg] = valor & 0xffffu;
        break;
    case 14:
        d[14] = d[15] = valor;
        break;
    case 15:
        d[12] = d[13]; d[13] = d[14]; d[14] = d[15] = valor;
        break;
    case 28:
        d[9] = (valor & 31u) << 7;
        d[10] = ((valor >> 5) & 31u) << 7;
        d[11] = ((valor >> 10) & 31u) << 7;
        break;
    case 29: case 31:
        break;
    case 30:
        d[30] = valor;
        d[31] = zeros_iniciais(valor);
        break;
    default:
        d[reg] = valor;
        break;
    }
}


/* V30: comandos GTE que o jogo usa (gte.c gera gte_k_<cmd> para cada um) */
#define GTE_LISTA_K(X) X(108041B) X(118043F) X(138041C) X(1400006) X(145545C) X(14B465D) X(178000C) \
    X(180001) X(190003D) X(198003D) X(280030) X(41E012) X(486012) X(49E012) X(4A3268) X(4A465C) X(4A6412) \
    X(A00428) X(A80428)
#define GTE_K_DECL(x) void gte_k_##x(CPUState *cpu);
GTE_LISTA_K(GTE_K_DECL)
#undef GTE_K_DECL
#endif
