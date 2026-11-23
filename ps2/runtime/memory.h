#ifndef PS2_MEMORY_H
#define PS2_MEMORY_H

#include "cpu_state.h"

void ps2_memory_init(CPUState *cpu);
uint8_t psx_read_byte(uint32_t address);
uint16_t psx_read_half(uint32_t address);
uint32_t psx_read_word(uint32_t address);
void psx_write_byte(uint32_t address, uint8_t value);
void psx_write_half(uint32_t address, uint16_t value);
void psx_write_word(uint32_t address, uint32_t value);

#endif
