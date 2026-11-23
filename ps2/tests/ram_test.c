#include "../runtime/ram.h"

#include <assert.h>
#include <stdint.h>

int main(void)
{
    uint8_t byte;
    uint16_t half;
    uint32_t word;
    uint32_t offset;

    ps2_ram_clear();

    assert(ps2_ram_write32(0x00001000u, 0x78563412u));
    assert(ps2_ram_read32(0x80201000u, &word));
    assert(word == 0x78563412u);
    assert(ps2_ram_read8(0xA0201001u, &byte));
    assert(byte == 0x34u);
    assert(ps2_ram_read16(0x00601000u, &half));
    assert(half == 0x3412u);

    /* Ultimo half da RAM (alinhado). No PS1 acesso desalinhado gera
     * excecao; aqui ele e alinhado para baixo. */
    assert(ps2_ram_write16(0x001FFFFEu, 0xBBAAu));
    assert(ps2_ram_read8(0x001FFFFEu, &byte) && byte == 0xAAu);
    assert(ps2_ram_read8(0x001FFFFFu, &byte) && byte == 0xBBu);
    assert(ps2_ram_read16(0x001FFFFFu, &half) && half == 0xBBAAu);

    assert(ps2_ram_offset(0x807FFFFFu, &offset));
    assert(offset == PS2_RAM_SIZE - 1u);
    assert(!ps2_ram_offset(0x00800000u, &offset));
    assert(!ps2_ram_offset(0x20001000u, &offset));
    assert(!ps2_ram_offset(0x60001000u, &offset));
    assert(!ps2_ram_offset(0xC0001000u, &offset));
    assert(!ps2_ram_read32(0x00800000u, &word));
    assert(!ps2_ram_write8(0x00800000u, 0xFFu));
    assert(!ps2_ram_read8(0u, 0));

    ps2_ram_clear();
    assert(ps2_ram_read32(0x00001000u, &word));
    assert(word == 0u);

    return 0;
}
