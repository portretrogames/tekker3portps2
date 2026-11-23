/*
 * tests/fast_memory_test.c — V46: o caminho rápido de memória do código
 * gerado (runtime/fast_memory.h) dá o mesmo resultado que memory.c para
 * todo endereço válido: RAM em KUSEG, KSEG0 e KSEG1 com os espelhos,
 * scratchpad em KUSEG e KSEG0, e I/O. Nas gravações confere também a RAM, o
 * scratchpad e a marcação de código sobrescrito (g_cod_sujo, g_cod_ver).
 */
#include "../runtime/memory.h"
#include "../runtime/io.h"
#include "../runtime/ram.h"
#include "../runtime/fast_memory.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUJO_BYTES PS2_COD_SUJO_BYTES

static unsigned long casos;
static uint32_t semente = 12345u;
static uint32_t aleatorio(void) { semente = semente * 1103515245u + 12345u; return semente >> 1; }

static const uint32_t segmentos[3] = { 0x00000000u, 0x80000000u, 0xA0000000u };
static const uint32_t espelhos[4] = { 0x000000u, 0x200000u, 0x400000u, 0x600000u };

static void ler_igual(CPUState *cpu, uint32_t a)
{
    assert(ps2_fast_read_word(a & ~3u) == psx_read_word(a & ~3u));
    assert(ps2_fast_read_half(a & ~1u) == psx_read_half(a & ~1u));
    assert(ps2_fast_read_byte(a) == psx_read_byte(a));
    assert(ps2_fast_cyc_load_word(cpu, a & ~3u, 2u, 0u) == psx_read_word(a & ~3u));
    assert(ps2_fast_cyc_load_half(cpu, a & ~1u, 2u, 0u) == psx_read_half(a & ~1u));
    assert(ps2_fast_cyc_load_byte(cpu, a, 2u, 0u) == psx_read_byte(a));
    casos++;
}

/* Grava pelos dois caminhos a partir do mesmo estado e compara o estado final. */
static uint8_t ram_ini[PS2_RAM_SIZE], ram_lento[PS2_RAM_SIZE];
static uint8_t spad_ini[1024], spad_lento[1024], sujo_ini[SUJO_BYTES], sujo_lento[SUJO_BYTES];
static void gravar_igual(uint32_t a, uint32_t v, int largura)
{
    uint32_t ver_ini = g_cod_ver, ver_lento;
    memcpy(ram_ini, ps2_ram_data(), PS2_RAM_SIZE);
    memcpy(spad_ini, g_ps2_scratchpad, 1024u);
    memcpy(sujo_ini, g_cod_sujo, SUJO_BYTES);
    if (largura == 4) psx_write_word(a & ~3u, v);
    else if (largura == 2) psx_write_half(a & ~1u, (uint16_t)v);
    else psx_write_byte(a, (uint8_t)v);
    memcpy(ram_lento, ps2_ram_data(), PS2_RAM_SIZE);
    memcpy(spad_lento, g_ps2_scratchpad, 1024u);
    memcpy(sujo_lento, g_cod_sujo, SUJO_BYTES);
    ver_lento = g_cod_ver;
    memcpy(ps2_ram_data(), ram_ini, PS2_RAM_SIZE);
    memcpy(g_ps2_scratchpad, spad_ini, 1024u);
    memcpy(g_cod_sujo, sujo_ini, SUJO_BYTES);
    g_cod_ver = ver_ini;
    if (largura == 4) ps2_fast_write_word(a & ~3u, v);
    else if (largura == 2) ps2_fast_write_half(a & ~1u, (uint16_t)v);
    else ps2_fast_write_byte(a, (uint8_t)v);
    assert(memcmp(ram_lento, ps2_ram_data(), PS2_RAM_SIZE) == 0);
    assert(memcmp(spad_lento, g_ps2_scratchpad, 1024u) == 0);
    assert(memcmp(sujo_lento, g_cod_sujo, SUJO_BYTES) == 0);
    assert(ver_lento == g_cod_ver);
    casos++;
}

int main(void)
{
    static CPUState cpu;
    uint32_t i, s, m, a;
    uint8_t *ram;

    ps2_memory_init(&cpu);
    ram = ps2_ram_data();
    for (i = 0; i < PS2_RAM_SIZE; i++) ram[i] = (uint8_t)(aleatorio() >> 7);
    for (i = 0; i < 1024u; i++) g_ps2_scratchpad[i] = (uint8_t)(aleatorio() >> 9);
    /* "executável" em 0x10000-0x30000: gravações ali marcam código sobrescrito */
    ps2_cod_registrar(0x80010000u, 0x20000u);

    /* ps2_kseg0_ram é exatamente a RAM do segmento KSEG0 */
    for (i = 0; i < 0x100000u; i++) {
        a = (i << 12) | (aleatorio() & 0xFFFu);
        assert(ps2_kseg0_ram(a) == (ps2_ram_hit(a) && (a >> 29) == 4u));
    }
    assert(ps2_kseg0_ram(0x80000000u) && ps2_kseg0_ram(0x807FFFFFu));
    assert(!ps2_kseg0_ram(0x7FFFFFFFu) && !ps2_kseg0_ram(0x80800000u) && !ps2_kseg0_ram(0xA0000000u));

    /* RAM: leituras em todos os segmentos e espelhos (bordas e aleatórias) */
    for (s = 0; s < 3u; s++)
        for (m = 0; m < 4u; m++) {
            static const uint32_t bordas[] = { 0u, 1u, 2u, 3u, 0x10000u, 0x2FFFFu, 0x1FFFFCu, 0x1FFFFEu, 0x1FFFFFu };
            for (i = 0; i < sizeof bordas / sizeof bordas[0]; i++) ler_igual(&cpu, segmentos[s] | (espelhos[m] + bordas[i]));
            for (i = 0; i < 20000u; i++) ler_igual(&cpu, segmentos[s] | (espelhos[m] + (aleatorio() & PS2_RAM_MASK)));
        }
    /* scratchpad: KUSEG e KSEG0 (KSEG1 não tem no PS1) */
    for (s = 0; s < 2u; s++)
        for (i = 0; i < 1024u; i++) ler_igual(&cpu, segmentos[s] | (0x1F800000u + i));

    /* gravações: RAM (dentro e fora do "executável"), espelhos, scratchpad */
    for (i = 0; i < 1500u; i++) {
        uint32_t o = (i & 1u) ? (0x10000u + (aleatorio() % 0x20000u)) : (aleatorio() & PS2_RAM_MASK);
        a = segmentos[aleatorio() % 3u] | (espelhos[aleatorio() & 3u] + o);
        gravar_igual(a, aleatorio(), (int)(1u << (aleatorio() % 3u)));
    }
    for (i = 0; i < 400u; i++) {
        a = segmentos[aleatorio() & 1u] | (0x1F800000u + (aleatorio() & 0x3FFu));
        gravar_igual(a, aleatorio(), (int)(1u << (aleatorio() % 3u)));
    }

    /* I/O pelo caminho rápido: I_MASK (grava e lê) e GPUSTAT (KSEG1). O
     * GPUSTAT tem bit que alterna entre leituras: confere os bits de pronto. */
    ps2_fast_write_half(0x1F801074u, 0x0005u);
    assert(ps2_fast_cyc_load_half(&cpu, 0x1F801074u, 2u, 0u) == 0x0005u);
    assert(psx_read_half(0x1F801074u) == 0x0005u);
    assert((ps2_fast_cyc_load_word(&cpu, 0xBF801814u, 2u, 0u) & 0x1C000000u) == 0x1C000000u);
    casos += 2;

    printf("Memoria rapida x memory.c: %lu casos, 0 diferencas\n", casos);
    return 0;
}
