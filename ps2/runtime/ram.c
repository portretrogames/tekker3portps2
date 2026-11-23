/* ram.c — RAM principal do PS1 (ver ram.h). */
#include "ram.h"
#include "quente.h"

#include <string.h>

/* V56: janela única da RAM e do scratchpad (ver ram.h: g_ps1_ram e
 * g_ps2_scratchpad são macros para dentro dela). Alinhada em 4 KB (ram.h:
 * lugar fixo no cache de dados). */
uint8_t g_ps2_janela[PS2_JANELA_TAM] __attribute__((aligned(4096)));
_Static_assert(PS2_JANELA_SPAD >= PS2_RAM_SIZE && (PS2_JANELA_MASK & 0x1F800000u) == PS2_JANELA_SPAD,
               "scratchpad (0x1F800000) & máscara cai no começo dele na janela");

/* V56: bloco quente (ram.h: lugar fixo no cache de dados). O estado da CPU
 * do PS1 (g_ps2_cpu, usado por main.c) e o mapa de páginas com código
 * (g_cod_pag) são apelidos para dentro dele. */
#define RAM_STR2(x) #x
#define RAM_STR(x) RAM_STR2(x)
PS2_QUENTE uint8_t g_ps2_quente[4096] __attribute__((aligned(4096)));   /* 1º da seção .bss.quente (quente.h) */
typedef char cod_pag_cabe[(PS2_COD_PAG_NO_CACHE + (PS2_RAM_SIZE >> PS2_COD_PAG_BITS) <= 4096u) ? 1 : -1];
__asm__("\t.globl g_ps2_cpu\n"
        "\t.set g_ps2_cpu, g_ps2_quente + " RAM_STR(PS2_CPU_NO_CACHE) "\n"
        "\t.globl g_cod_pag\n"
        "\t.set g_cod_pag, g_ps2_quente + " RAM_STR(PS2_COD_PAG_NO_CACHE) "\n");

void ps2_ram_clear(void)
{
    memset(g_ps1_ram, 0, PS2_RAM_SIZE);
}

uint8_t *ps2_ram_data(void)
{
    return g_ps1_ram;
}

int ps2_ram_offset(uint32_t address, uint32_t *offset)
{
    if (offset == 0 || !ps2_ram_hit(address))
        return 0;
    *offset = address & PS2_RAM_MASK;
    return 1;
}

int ps2_ram_read8(uint32_t address, uint8_t *value)
{
    if (value == 0 || !ps2_ram_hit(address))
        return 0;
    *value = ps2_ram_get8(address);
    return 1;
}

int ps2_ram_read16(uint32_t address, uint16_t *value)
{
    if (value == 0 || !ps2_ram_hit(address))
        return 0;
    *value = ps2_ram_get16(address);
    return 1;
}

int ps2_ram_read32(uint32_t address, uint32_t *value)
{
    if (value == 0 || !ps2_ram_hit(address))
        return 0;
    *value = ps2_ram_get32(address);
    return 1;
}

int ps2_ram_write8(uint32_t address, uint8_t value)
{
    if (!ps2_ram_hit(address))
        return 0;
    ps2_ram_set8(address, value);
    return 1;
}

int ps2_ram_write16(uint32_t address, uint16_t value)
{
    if (!ps2_ram_hit(address))
        return 0;
    ps2_ram_set16(address, value);
    return 1;
}

int ps2_ram_write32(uint32_t address, uint32_t value)
{
    if (!ps2_ram_hit(address))
        return 0;
    ps2_ram_set32(address, value);
    return 1;
}
