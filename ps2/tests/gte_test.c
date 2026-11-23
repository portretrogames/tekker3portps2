#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cpu_state.h"

/* V45: duas metades de 16 bits, cada uma em [-(m+1), m] (sinal centrado em zero) */
static uint32_t centr(uint32_t v, uint32_t m)
{
    uint32_t lo = (uint32_t)(uint16_t)((int32_t)(v & (2u * m + 1u)) - (int32_t)(m + 1u));
    uint32_t hi = (uint32_t)(uint16_t)((int32_t)((v >> 16) & (2u * m + 1u)) - (int32_t)(m + 1u));
    return lo | (hi << 16);
}

int main(void)
{
    CPUState cpu;
    memset(&cpu, 0, sizeof(cpu));
    gte_write_data(&cpu, 12, 1);
    gte_write_data(&cpu, 13, 2);
    gte_write_data(&cpu, 14, 3);
    gte_write_data(&cpu, 15, 4);
    assert(gte_read_data(&cpu, 12) == 2);
    assert(gte_read_data(&cpu, 13) == 3);
    assert(gte_read_data(&cpu, 14) == 4);
    assert(gte_read_data(&cpu, 15) == 4);

    gte_write_data(&cpu, 28, 0x7c41);
    assert(gte_read_data(&cpu, 9) == 128);
    assert(gte_read_data(&cpu, 10) == 256);
    assert(gte_read_data(&cpu, 11) == 3968);
    assert(gte_read_data(&cpu, 29) == 0x7c41);
    gte_write_data(&cpu, 9, 0xffff);
    gte_write_data(&cpu, 10, 0x7fff);
    assert(gte_read_data(&cpu, 28) == 0x7fe0);
    gte_write_data(&cpu, 29, 0);
    assert(gte_read_data(&cpu, 29) == 0x7fe0);

    gte_write_data(&cpu, 30, 0);
    assert(gte_read_data(&cpu, 31) == 32);
    gte_write_data(&cpu, 30, 0xfffffff0u);
    assert(gte_read_data(&cpu, 31) == 28);
    gte_write_data(&cpu, 31, 0);
    assert(gte_read_data(&cpu, 31) == 28);

    gte_write_data(&cpu, 1, 0x8000);
    assert(gte_read_data(&cpu, 1) == 0xffff8000u);
    gte_write_ctrl(&cpu, 26, 0xffff);
    assert(gte_read_ctrl(&cpu, 26) == 0xffffffffu);
    gte_write_ctrl(&cpu, 31, 0xffffffffu);
    assert(gte_read_ctrl(&cpu, 31) == 0xfffff000u);
    gte_write_ctrl(&cpu, 31, 0x00400000u);
    assert(gte_read_ctrl(&cpu, 31) == 0x00400000u);
    /* ---- comandos ---- */
    memset(&cpu, 0, sizeof(cpu));
    /* NCLIP: triângulo (0,0) (10,0) (0,10) -> 100 */
    gte_write_data(&cpu, 12, 0);
    gte_write_data(&cpu, 13, 10);
    gte_write_data(&cpu, 14, 10u << 16);
    gte_execute(&cpu, 0x06);
    assert((int32_t)gte_read_data(&cpu, 24) == 100);

    /* AVSZ3: ZSF3=1365, SZ1..3=300 -> MAC0=1228500, OTZ=299 */
    gte_write_ctrl(&cpu, 29, 1365);
    gte_write_data(&cpu, 17, 300); gte_write_data(&cpu, 18, 300); gte_write_data(&cpu, 19, 300);
    gte_execute(&cpu, 0x2D);
    assert(gte_read_data(&cpu, 24) == 1228500u);
    assert(gte_read_data(&cpu, 7) == 299u);

    /* RTPS: RT identidade, TR=(0,0,1000), V0=(100,50,0), H=1000, sf=1 */
    memset(&cpu, 0, sizeof(cpu));
    gte_write_ctrl(&cpu, 0, 0x1000);            /* RT11=1.0, RT12=0 */
    gte_write_ctrl(&cpu, 2, 0x1000);            /* RT22=1.0, RT23=0 */
    gte_write_ctrl(&cpu, 4, 0x1000);            /* RT33=1.0 */
    gte_write_ctrl(&cpu, 7, 1000);              /* TRZ */
    gte_write_ctrl(&cpu, 26, 1000);             /* H */
    gte_write_data(&cpu, 0, (50u << 16) | 100u);
    gte_write_data(&cpu, 1, 0);
    gte_execute(&cpu, (1u << 19) | 0x01);
    assert(gte_read_data(&cpu, 25) == 100u && gte_read_data(&cpu, 26) == 50u);
    assert(gte_read_data(&cpu, 19) == 1000u);   /* SZ3 */
    {
        uint32_t sxy = gte_read_data(&cpu, 14);
        int sx = (int16_t)(sxy & 0xFFFF), sy = (int16_t)(sxy >> 16);
        assert(sx >= 99 && sx <= 100 && sy >= 49 && sy <= 50);
    }
    assert((gte_read_ctrl(&cpu, 31) & 0x80000000u) == 0u);   /* sem erro */

    /* SQR: IR=(3,4,5), sf=0 -> MAC=(9,16,25) */
    gte_write_data(&cpu, 9, 3); gte_write_data(&cpu, 10, 4); gte_write_data(&cpu, 11, 5);
    gte_execute(&cpu, 0x28);
    assert(gte_read_data(&cpu, 25) == 9u && gte_read_data(&cpu, 26) == 16u &&
           gte_read_data(&cpu, 27) == 25u);

    /* V45: caminho rápido (g_gte_rapido = 1, ex.: RTPS/RTPT de rtp_rapido) x
     * caminho completo (0), estados aleatórios. Três tipos de estado: tudo
     * aleatório, valores pequenos (o rápido entra quase sempre) e "de jogo"
     * (TR pequeno, Z positivo, H típico: exercita a divisão e as filas). */
    {
        extern int g_gte_rapido;
        static const uint32_t ops[] = { 0x01, 0x30, 0x12, 0x13, 0x16, 0x1B, 0x1C, 0x1E, 0x20, 0x3F, 0x14,
                                        0x06, 0x2D, 0x2E, 0x28, 0x29, 0x3D, 0x3E, 0x10, 0x2A, 0x11, 0x0C };
        static CPUState a, b;
        uint32_t sem = 7654321u;
        unsigned long k, dif = 0, n_rtpt = 0;
        unsigned i;
        /* valores nos limites das saturações (IR ±0x8000, SZ 0..0xFFFF, SX/SY ±0x400, IR0 0..0x1000) */
        static const uint32_t borda[] = { 0, 1, 0xFFFFFFFFu, 0x7FFE, 0x7FFF, 0x8000, 0x8001,
            (uint32_t)-0x7FFF, (uint32_t)-0x8000, (uint32_t)-0x8001, 0xFFFE, 0xFFFF, 0x10000, 0x10001,
            0x3FFu << 16, 0x400u << 16, (0x3FFu << 16) | 0xFFFFu, (uint32_t)-(0x400 << 16), (uint32_t)-(0x401 << 16),
            0x1000, 0x1001, 0x0FFF, 0x7FFFFFFFu, 0x80000000u, 0x7FFF000u, 0x8000000u, 0xFFFF000u, 0x10000000u };
#define BORDA() (sem = sem * 1103515245u + 12345u, borda[(sem >> 9) % (sizeof borda / sizeof borda[0])])
        for (k = 0; k < 800000ul; k++) {
            uint32_t cmd, tipo = (uint32_t)(k % 4u);
            for (i = 0; i < 32; i++) {
                sem = sem * 1103515245u + 12345u; a.gte_data[i] = sem ^ (sem >> 13);
                sem = sem * 1103515245u + 12345u; a.gte_ctrl[i] = sem ^ (sem >> 11);
                if (tipo == 1) { a.gte_data[i] = centr(a.gte_data[i], 0x0FFF); a.gte_ctrl[i] = centr(a.gte_ctrl[i], 0x0FFF); }
            }
            if (tipo == 2) {   /* estado parecido com o do jogo */
                for (i = 0; i < 6; i++) a.gte_data[i] = centr(a.gte_data[i], 0x03FF);   /* vértices */
                for (i = 0; i < 5; i++) a.gte_ctrl[i] = centr(a.gte_ctrl[i], 0x1FFF);   /* matriz */
                a.gte_ctrl[5] = (uint32_t)((int32_t)(a.gte_ctrl[5] & 0x7FFu) - 0x400);
                a.gte_ctrl[6] = (uint32_t)((int32_t)(a.gte_ctrl[6] & 0x7FFu) - 0x400);
                a.gte_ctrl[7] = 0x400u + (a.gte_ctrl[7] & 0x3FFFu);             /* TRZ > 0 */
                a.gte_ctrl[24] = (a.gte_ctrl[24] & 0x01FFFFFFu) - 0x01000000u;      /* OFX/OFY com sinal */
                a.gte_ctrl[25] = (a.gte_ctrl[25] & 0x01FFFFFFu) - 0x01000000u;
                a.gte_ctrl[26] = 0x100u + (a.gte_ctrl[26] & 0x3FFu);            /* H */
            }
            if (tipo == 3) {   /* limites: matriz zero ou pequena, TR/OF/DQ/H/IR nas bordas */
                for (i = 0; i < 6; i++) a.gte_data[i] = centr(a.gte_data[i], 0x000F);
                for (i = 0; i < 5; i++) a.gte_ctrl[i] = (k & 4u) ? 0u : centr(a.gte_ctrl[i], 0x0003);
                for (i = 5; i < 8; i++) a.gte_ctrl[i] = BORDA();
                a.gte_ctrl[24] = BORDA(); a.gte_ctrl[25] = BORDA();
                a.gte_ctrl[26] = BORDA() & 0xFFFFu; a.gte_ctrl[27] = BORDA(); a.gte_ctrl[28] = BORDA();
                for (i = 8; i < 12; i++) a.gte_data[i] = BORDA();
                for (i = 16; i < 20; i++) a.gte_data[i] = BORDA() & 0xFFFFu;
            }
            for (i = 1; i < 12; i++) if (i == 1 || i == 3 || i == 5 || (i >= 8 && i <= 11))
                a.gte_data[i] = (uint32_t)(int32_t)(int16_t)a.gte_data[i];
            sem = sem * 1103515245u + 12345u;
            /* bits 13..24: cv, v, mx, sf (MVMVA) e o resto do comando; bit 10: lm */
            cmd = ops[(sem >> 8) % (sizeof ops / sizeof ops[0])] | (sem & 0x1FFE000u) | ((sem << 3) & (1u << 10));
            if ((cmd & 0x3Fu) == 0x30) n_rtpt++;
            b = a;
            g_gte_rapido = 1; gte_execute(&a, cmd);
            g_gte_rapido = 0; gte_execute(&b, cmd);
            if (memcmp(a.gte_data, b.gte_data, sizeof a.gte_data) || memcmp(a.gte_ctrl, b.gte_ctrl, sizeof a.gte_ctrl)) {
                if (dif++ < 5) printf("GTE: rapido x completo DIFERENTE: cmd %08lx tipo %u\n", (unsigned long)cmd, tipo);
            }
        }
#undef BORDA
        g_gte_rapido = 1;
        printf("GTE: rapido x completo: %lu casos (%lu RTPT), %lu diferencas\n", k, n_rtpt, dif);
        assert(dif == 0);
    }

    puts("GTE: registradores e comandos OK");
    return 0;
}
