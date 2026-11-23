/*
 * memory.c — caminho "lento" da memória do PS1: RAM (para quem não usa o
 * inline), scratchpad de 1 KB e I/O.
 *
 * Endereço que não é RAM, scratchpad, E/S nem o controle de cache PARA a
 * execução (abort): o jogo nunca deveria chegar lá.
 */
#include "memory.h"
#include "ram.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PSX_ENABLE_BLOCK_CYCLES
#error "O runtime PS2 nao usa contagem de ciclos (PSX_ENABLE_BLOCK_CYCLES)"
#endif

/* Acessos à RAM que chegam aqui são raros: o código gerado só manda KUSEG/KSEG1;
 * KSEG0 é resolvido em linha em fast_memory.h. */
#define SCRATCHPAD_BASE 0x1F800000u
#define SCRATCHPAD_SIZE 1024u

/* V56: definido em ram.c, dentro da janela da RAM (ram.h) */
#define scratchpad g_ps2_scratchpad

static void memory_unimplemented(uint32_t address, unsigned width, int write,
                                 uint32_t value)
{
    (void)address; (void)width; (void)write; (void)value;
    abort();
}

/* Scratchpad só existe em KUSEG e KSEG0 (não em KSEG1). */
/* Registrador de controle de cache (KSEG2 0xFFFE0130): só guarda. */
#define CACHE_CTRL 0xFFFE0130u
static uint32_t cache_ctrl;

static int scratchpad_hit(uint32_t address, uint32_t *offset)
{
    uint32_t seg = address >> 29;
    uint32_t phys = address & 0x1FFFFFFFu;

    if (!(seg == 0u || seg == 4u))
        return 0;
    if (phys - SCRATCHPAD_BASE >= SCRATCHPAD_SIZE)
        return 0;
    *offset = phys - SCRATCHPAD_BASE;
    return 1;
}

void ps2_memory_init(CPUState *cpu)
{
    if (cpu == NULL) {
        abort();
    }
    ps2_ram_clear();
    memset(scratchpad, 0, SCRATCHPAD_SIZE);
    ps2_io_init();
    cache_ctrl = 0;
    cpu->read_byte = psx_read_byte;
    cpu->read_half = psx_read_half;
    cpu->read_word = psx_read_word;
    cpu->write_byte = psx_write_byte;
    cpu->write_half = psx_write_half;
    cpu->write_word = psx_write_word;
}

uint8_t psx_read_byte(uint32_t address)
{
    uint32_t off;
    if (ps2_ram_hit(address)) {
        return ps2_ram_get8(address);
    }
    if (scratchpad_hit(address, &off)) { return scratchpad[off]; }
    if (ps2_io_hit(address))
        return (uint8_t)ps2_io_read(address, 1u);
    if (address == CACHE_CTRL)
        return (uint8_t)cache_ctrl;
    memory_unimplemented(address, 1u, 0, 0);
    return 0;
}

uint16_t psx_read_half(uint32_t address)
{
    uint32_t off;
    if (ps2_ram_hit(address)) {
        return ps2_ram_get16(address);
    }
    if (scratchpad_hit(address, &off)) { return *(const ps2_u16a *)&scratchpad[off & ~1u]; }
    if (ps2_io_hit(address))
        return (uint16_t)ps2_io_read(address, 2u);
    if (address == CACHE_CTRL)
        return (uint16_t)cache_ctrl;
    memory_unimplemented(address, 2u, 0, 0);
    return 0;
}

uint32_t psx_read_word(uint32_t address)
{
    uint32_t off;
    if (ps2_ram_hit(address)) {
        return ps2_ram_get32(address);
    }
    if (scratchpad_hit(address, &off)) { return *(const ps2_u32a *)&scratchpad[off & ~3u]; }
    if (ps2_io_hit(address))
        return ps2_io_read(address, 4u);
    if (address == CACHE_CTRL)
        return cache_ctrl;
    memory_unimplemented(address, 4u, 0, 0);
    return 0;
}

void psx_write_byte(uint32_t address, uint8_t value)
{
    uint32_t off;
    if (ps2_ram_hit(address)) { ps2_ram_set8(address, value); return; }
    if (scratchpad_hit(address, &off)) { scratchpad[off] = value; return; }
    if (ps2_io_hit(address)) { ps2_io_write(address, value, 1u); return; }
    if (address == CACHE_CTRL) { cache_ctrl = value; return; }
    memory_unimplemented(address, 1u, 1, value);
}

void psx_write_half(uint32_t address, uint16_t value)
{
    uint32_t off;
    if (ps2_ram_hit(address)) { ps2_ram_set16(address, value); return; }
    if (scratchpad_hit(address, &off)) {
        *(ps2_u16a *)&scratchpad[off & ~1u] = value;
        return;
    }
    if (ps2_io_hit(address)) { ps2_io_write(address, value, 2u); return; }
    if (address == CACHE_CTRL) { cache_ctrl = value; return; }
    memory_unimplemented(address, 2u, 1, value);
}

void psx_write_word(uint32_t address, uint32_t value)
{
    uint32_t off;
    if (ps2_ram_hit(address)) { ps2_ram_set32(address, value); return; }
    if (scratchpad_hit(address, &off)) {
        *(ps2_u32a *)&scratchpad[off & ~3u] = value;
        return;
    }
    if (ps2_io_hit(address)) { ps2_io_write(address, value, 4u); return; }
    if (address == CACHE_CTRL) { cache_ctrl = value; return; }
    memory_unimplemented(address, 4u, 1, value);
}

/* Usadas pelo código gerado quando o inline não se aplica. */
uint8_t psx_cyc_load_byte(CPUState *cpu, uint32_t address, uint32_t rt,
                          uint32_t reg_mask)
{
    (void)cpu; (void)rt; (void)reg_mask;
    return psx_read_byte(address);
}

uint32_t psx_cyc_lwc2_read(CPUState *cpu, uint32_t address)
{
    (void)cpu;
    return psx_read_word(address);
}
