#include "../runtime/memory.h"
#include "../runtime/io.h"
#include "../runtime/ram.h"

#include <assert.h>
#include <string.h>

int main(int argc, char **argv)
{
    CPUState cpu;

    memset(&cpu, 0, sizeof(cpu));
    ps2_memory_init(&cpu);
    if (argc > 1 && strcmp(argv[1], "io") == 0) {
        psx_read_word(0x1F000000u);  /* expansao 1: nao mapeada */
        return 1;
    }

    cpu.write_word(0x80001234u, 0x78563412u);
    assert(cpu.read_word(0xA0201234u) == 0x78563412u);
    assert(cpu.read_half(0x1234u) == 0x3412u);
    assert(psx_cyc_load_byte(&cpu, 0x1235u, 2u, 0u) == 0x34u);
    assert(psx_cyc_lwc2_read(&cpu, 0x1234u) == 0x78563412u);

    cpu.write_word(0x1F800000u, 0x78563412u);
    assert(cpu.read_word(0x9F800000u) == 0x78563412u);
    /* No PS1 o scratchpad nao existe em KSEG1 (0xBF80...), so KUSEG/KSEG0. */
    assert(cpu.read_half(0x9F800002u) == 0x7856u);
    cpu.write_half(0x1F8003FEu, 0xABCDu);
    assert(cpu.read_byte(0x1F8003FFu) == 0xABu);
    cpu.write_byte(0x1F800001u, 0xEEu);
    assert(cpu.read_word(0x1F800000u) == 0x7856EE12u);

    /* I/O: interrupcoes */
    cpu.write_half(0x1F801074u, 0x0005u);
    assert(cpu.read_half(0x1F801074u) == 0x0005u);
    ps2_irq_raise(IRQ_VBLANK);
    ps2_irq_raise(IRQ_CDROM);
    assert(cpu.read_half(0x1F801070u) == 0x0005u);
    cpu.write_half(0x1F801070u, (uint16_t)~1u);       /* ack do VBLANK */
    assert(cpu.read_half(0x1F801070u) == 0x0004u);
    /* GPUSTAT pronto */
    assert((cpu.read_word(0xBF801814u) & 0x1C000000u) == 0x1C000000u);
    /* DMA2 linked list: envia um preenchimento à GPU. */
    cpu.write_word(0x1000u, 0x03ffffffu);
    cpu.write_word(0x1004u, 0x020000ffu);
    cpu.write_word(0x1008u, 0x00010000u);
    cpu.write_word(0x100cu, 0x00010010u);
    cpu.write_word(0x1f8010a0u, 0x1000u);
    cpu.write_word(0x1f8010a4u, 0u);
    cpu.write_word(0x1f8010a8u, 0x01000401u);
    assert((cpu.read_word(0x1f8010a8u) & (1u << 24)) == 0);
    cpu.write_word(0x1f801810u, 0xc0000000u);
    cpu.write_word(0x1f801810u, 0x00010000u);
    cpu.write_word(0x1f801810u, 0x00010002u);
    cpu.write_word(0x1f8010a0u, 0x2000u);
    cpu.write_word(0x1f8010a4u, 1u);
    cpu.write_word(0x1f8010a8u, 0x11000000u);
    assert(cpu.read_word(0x2000u) == 0x001f001fu);
    /* DMA2 em bloco envia dados CPU -> VRAM. */
    cpu.write_word(0x1f801810u, 0xa0000000u);
    cpu.write_word(0x1f801810u, 0x00020000u);
    cpu.write_word(0x1f801810u, 0x00010002u);
    cpu.write_word(0x2004u, 0x7c0003e0u);
    cpu.write_word(0x1f8010a0u, 0x2004u);
    cpu.write_word(0x1f8010a4u, 0x00010001u);
    cpu.write_word(0x1f8010a8u, 0x01000201u);
    cpu.write_word(0x1f801810u, 0xc0000000u);
    cpu.write_word(0x1f801810u, 0x00020000u);
    cpu.write_word(0x1f801810u, 0x00010002u);
    assert(cpu.read_word(0x1f801810u) == 0x7c0003e0u);
    /* DMA6 (OTC): 4 entradas terminando em 0x1000 */
    cpu.write_word(0x1F8010E0u, 0x8000100Cu);
    cpu.write_word(0x1F8010E4u, 4u);
    cpu.write_word(0x1F8010E8u, 0x11000002u);
    assert((cpu.read_word(0x1F8010E8u) & (1u << 24)) == 0u);
    assert(cpu.read_word(0x8000100Cu) == 0x00001008u);
    assert(cpu.read_word(0x80001004u) == 0x00001000u);
    assert(cpu.read_word(0x80001000u) == 0x00FFFFFFu);

    /* MDEC: ocioso -> ocupado com comando incompleto -> reset volta a ocioso. */
    assert(cpu.read_word(0x1F801824u) == 0x8004FFFFu);
    cpu.write_word(0x1F801820u, 0x40000001u);       /* tabelas: 32 parâmetros */
    assert(cpu.read_word(0x1F801824u) & (1u << 29));
    cpu.write_word(0x1F801824u, 0x80000000u);
    assert(cpu.read_word(0x1F801824u) == 0x8004FFFFu);
    assert(cpu.read_word(0x1F801820u) == 0u);

    /* DMA0 termina sem modificar a RAM; DMA1 sem dados do MDEC fica ocupado. */
    cpu.write_word(0x80002000u, 0x12345678u);
    cpu.write_word(0x1F801080u, 0x80002000u);
    cpu.write_word(0x1F801084u, 0x00010001u);
    cpu.write_word(0x1F801088u, 0x01000201u);
    assert((cpu.read_word(0x1F801088u) & ((1u << 24) | (1u << 28))) == 0u);
    assert(cpu.read_word(0x80002000u) == 0x12345678u);

    cpu.write_word(0x80002004u, 0x12345678u);
    cpu.write_word(0x80002008u, 0x12345678u);
    cpu.write_word(0x8000200Cu, 0x12345678u);
    cpu.write_word(0x80002010u, 0x12345678u);
    cpu.write_word(0x1F801074u, 1u << IRQ_DMA);
    cpu.write_word(0x1F8010F4u, (1u << 17) | (1u << 23));
    cpu.write_word(0x1F801090u, 0x80002000u);
    cpu.write_word(0x1F801094u, 0x00020002u);
    cpu.write_word(0x1F801098u, 0x01000200u);
    assert(cpu.read_word(0x1F801098u) & (1u << 24));
    assert(cpu.read_word(0x80002000u) == 0x12345678u);
    assert(cpu.read_word(0x80002010u) == 0x12345678u);
    assert((cpu.read_word(0x1F8010F4u) & (1u << 25)) == 0u);   /* sem IRQ ainda */
    /* (decodificação e DMA do MDEC: tests/mdec_test.c) */

    ps2_memory_init(&cpu);
    assert(cpu.read_word(0x1234u) == 0u);
    assert(cpu.read_word(0x1F800000u) == 0u);
    return 0;
}
