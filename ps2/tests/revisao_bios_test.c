/* Regressão do achado F3 (callback entregue fora de exceção não se perde) e
 * da auditoria V19 1.1 (func1 chama DeliverEvent: callback roda, func2 roda
 * uma vez com o v0 de func1, func1 não roda de novo). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../runtime/bios_hle.c"
void ps2_video_vblank(void) {}
int main(void)
{
    static CPUState c;
    unsigned h;
    memset(&c, 0, sizeof c);
    h = ev_abrir(0xF0000009u, 0x20u, EVMD_INTR, 0x80012340u);
    ev_de(h)->estado = EV_ATIVO;
    ev_entregar(0xF0000009u, 0x20u);
    if (setjmp(g_ps2_volta_ao_laco) == 0)
        entrar_na_excecao(&c, 0x80010000u);
    assert(c.pc == 0x80012340u);
    puts("Revisao (F3): OK");

    {   /* V19 1.1 */
        const uint32_t no = 0x80020000u, func1 = 0x80031000u, func2 = 0x80030000u;
        const uint32_t cb = 0x80012340u;
        memset(&c, 0, sizeof c);
        em_excecao = 0; cb_n = 0; so_callbacks = 0;
        ps2_ram_set32(no + 0, 0);            /* próximo */
        ps2_ram_set32(no + 4, func2);
        ps2_ram_set32(no + 8, func1);
        lista_irq[0] = no;
        if (setjmp(g_ps2_volta_ao_laco) == 0)
            entrar_na_excecao(&c, 0x80010000u);
        assert(c.pc == func1);
        /* func1 chama DeliverEvent (evento com callback, modo INTR) */
        ev_de(h)->estado = EV_ATIVO;
        c.gpr[9] = 0x07; c.gpr[4] = 0xF0000009u; c.gpr[5] = 0x20u; c.gpr[31] = func1 + 0x40u;
        ps2_bios_call(&c, 0xB0u);
        /* func1 devolve v0 = 5 */
        c.gpr[2] = 5;
        ps2_bios_call(&c, RET_MAGICO);
        assert(c.pc == cb);                  /* callback roda primeiro */
        c.gpr[2] = 0;                        /* o callback estraga v0 */
        ps2_bios_call(&c, RET_MAGICO);
        assert(c.pc == func2);               /* func2 roda com o v0 de func1 */
        assert(c.gpr[4] == 5u);
        ps2_bios_call(&c, RET_MAGICO);
        assert(c.pc == 0x80010000u);         /* volta ao jogo (func1 não repete) */
        assert(!em_excecao);
        puts("Revisao (V19 1.1, DeliverEvent dentro de func1): OK");
    }
    return 0;
}
