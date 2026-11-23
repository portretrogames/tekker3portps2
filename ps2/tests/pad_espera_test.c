/*
 * Esperas da libpad adiantadas (io.c: ps2_timer_pad_adianta, V56).
 *
 * Refaz em C as rotinas do Tekken 3 que conversam com o controle no VBlank,
 * na ordem em que o jogo as chama (desmontagem do SLUS_004.02):
 *   800951B8  guarda o contador do timer 2 e o prazo
 *   800951D8  compara (1 = prazo passou)
 *   800936AC  limpa a IRQ do controle no I_STAT e reconhece em JOY_CTRL
 *   800932A0  1º byte: espera o prazo e manda; demais: lê o byte recebido,
 *             espera o ACK testando o prazo ANTES de olhar o I_STAT, manda o
 *             próximo
 * e roda a conversa inteira das duas portas com o relógio andando em vários
 * passos e fases, e com tempo de CPU variado entre uma rotina e outra.
 *
 * Exige, com e sem o adianto: os 4 ACKs da porta 1 vistos, os 5 bytes certos,
 * a porta 2 sem resposta quando desligada e com os botões dela quando ligada
 * (V59). E, com o adianto, bem menos tempo de PS1 gasto.
 *
 * Motivo (console, 21/11/2026 — no PCSX2 as duas versões passavam):
 *  1. o prazo estourava na mesma leitura do contador que entregava o ACK;
 *  2. a leitura do contador entregava o ACK antes de o jogo limpar o I_STAT
 *     (depois de mandar um byte ele chama 800951B8 e só então 800936AC): o
 *     ACK era apagado e o jogo desistia do controle.
 *
 * Relógio controlado: compile com -Dclock=relogio_teste.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../runtime/io.h"
#include "../runtime/pad.h"

#define T2_CNT   0x1F801120u
#define T2_MODO  0x1F801124u
#define T2_ALVO  0x1F801128u
#define I_STAT   0x1F801070u
#define JOY_DATA 0x1F801040u
#define JOY_STAT 0x1F801044u
#define JOY_CTRL 0x1F80104Au

/* cada consulta ao relógio anda 1/passo de tique (1 tique = ~34 ciclos PS1) */
static unsigned long chamadas;
static unsigned passo = 1;
clock_t relogio_teste(void) { return (clock_t)(chamadas++ / passo); }
void ps2_video_vblank(void) {}

/* tempo de CPU entre duas rotinas do jogo (no console passam bem mais que os
 * ~100 ciclos do ACK entre mandar o byte e a leitura seguinte do contador) */
static unsigned cpu_tiques;
static void gasta(void) { chamadas += (unsigned long)cpu_tiques * passo; }

static uint32_t t_ini, t_prazo;
static unsigned long voltas;        /* voltas de laço de espera (trava de segurança) */

static void marca(uint32_t prazo)                   /* 800951B8 */
{
    t_ini = ps2_io_read(T2_CNT, 2);
    t_prazo = prazo;
}
static int passou(void)                             /* 800951D8 */
{
    uint32_t agora = ps2_io_read(T2_CNT, 2), e;
    assert(++voltas < 4000000ul);
    if (agora < t_ini) {
        uint32_t alvo = ps2_io_read(T2_ALVO, 2);
        agora += alvo ? alvo : 0x10000u;
    }
    e = agora - t_ini;
    if (!(ps2_io_read(T2_MODO, 2) & 0x200u)) e >>= 3;
    return !(e < t_prazo);
}

static int limpa_irq(void)                          /* 800936AC */
{
    ps2_io_write(I_STAT, ~0x80u, 4);
    if (ps2_io_read(JOY_STAT, 2) & 0x80u)
        do { if (passou()) return 0; } while (ps2_io_read(JOY_STAT, 2) & 0x80u);
    ps2_io_write(JOY_CTRL, ps2_io_read(JOY_CTRL, 2) | 0x10u, 2);
    return 1;
}

/* 800932A0 com a1 < 0: espera o prazo marcado por quem chamou e manda o 1º byte */
static void manda_primeiro(uint8_t tx)
{
    (void)ps2_io_read(JOY_DATA, 1);
    while (!(ps2_io_read(JOY_STAT, 2) & 1u)) {}
    while (!passou()) {}
    ps2_io_write(JOY_DATA, tx, 1);
}

/* 800932A0 com a1 >= 0: lê o byte recebido, espera o ACK e manda o próximo.
 * -20 = prazo estourado (o próximo byte não é mandado). */
static int recebe_e_manda(uint8_t *rx, uint8_t proximo)
{
    t_ini = ps2_io_read(T2_CNT, 2);                 /* em linha, antes de tudo */
    t_prazo = 430;
    while (!(ps2_io_read(JOY_STAT, 2) & 2u)) {}
    *rx = (uint8_t)ps2_io_read(JOY_DATA, 1);
    if (!(ps2_io_read(I_STAT, 4) & 0x80u))
        for (;;) {
            if (passou()) return -20;
            if (ps2_io_read(I_STAT, 4) & 0x80u) break;
        }
    ps2_io_write(JOY_DATA, proximo, 1);
    return 0;
}

/* Conversa com uma porta. Devolve quantos bytes tiveram ACK. */
static int conversa(int porta, uint8_t rx[5])
{
    static const uint8_t tx[6] = { 0x01, 0x42, 0x00, 0x00, 0x00, 0x00 };
    int i;
    ps2_io_write(JOY_CTRL, 0x1003u | (porta ? 0x2000u : 0u), 2);   /* seleciona, IRQ de ACK ligada */
    marca(80);
    gasta();
    manda_primeiro(tx[0]);
    for (i = 0; i < 5; i++) {
        gasta();
        marca(60);                                  /* 8009322C: lê o contador... */
        gasta();
        assert(limpa_irq());                        /* ...e só então limpa o I_STAT */
        gasta();
        if (recebe_e_manda(&rx[i], tx[i + 1]) != 0) break;
    }
    ps2_io_write(JOY_CTRL, 0, 2);
    return i;
}

/* Um VBlank da libpad: as duas portas. Devolve os ciclos de PS1 gastos. */
static uint32_t vblank(int adianta)
{
    uint8_t rx[5] = { 0, 0, 0, 0, 0 };
    uint32_t t0 = ps2_ps1_cycles();
    voltas = 0;                                         /* trava por VBlank */
    ps2_timer_pad_adianta(adianta);
    assert(conversa(0, rx) == 4);                       /* 4 ACKs; o 5º byte não tem */
    assert(rx[0] == 0xFF && rx[1] == 0x41 && rx[2] == 0x5A);
    assert(rx[3] == (uint8_t)ps2_pad_botoes[0] && rx[4] == (uint8_t)(ps2_pad_botoes[0] >> 8));
    memset(rx, 0, sizeof(uint8_t[5]));
    if (!ps2_pad_ligado[1])
        assert(conversa(1, rx) == 0);                   /* porta 2 vazia: ninguém responde */
    else {
        assert(conversa(1, rx) == 4);
        assert(rx[0] == 0xFF && rx[1] == 0x41 && rx[2] == 0x5A);
        assert(rx[3] == (uint8_t)ps2_pad_botoes[1] && rx[4] == (uint8_t)(ps2_pad_botoes[1] >> 8));
    }
    ps2_timer_pad_adianta(0);
    return ps2_ps1_cycles() - t0;
}

int main(void)
{
    static const unsigned passos[] = { 1, 2, 3, 5, 8, 13, 40 };
    unsigned p, fase, n = 0;
    uint64_t real = 0, adiantado = 0;

    ps2_io_init();
    {   /* V59: trocar de porta no meio da conversa encerra a anterior (a
         * porta 2 vazia não pode continuar a conversa da porta 1) */
        ps2_pad_ligado[1] = 0;
        ps2_io_write(JOY_CTRL, 0x0003u, 2);
        ps2_io_write(JOY_DATA, 0x01u, 1);
        assert((ps2_io_read(JOY_DATA, 1) & 0xFFu) == 0xFFu);
        ps2_io_write(JOY_CTRL, 0x2003u, 2);
        ps2_io_write(JOY_DATA, 0x42u, 1);
        assert((ps2_io_read(JOY_DATA, 1) & 0xFFu) == 0xFFu);   /* não 0x41 */
        ps2_io_write(JOY_CTRL, 0, 2);
    }
    ps2_pad_botoes[0] = 0xBF7Eu;
    ps2_pad_botoes[1] = 0xF7FEu;                        /* controle 2: R1 + SELECT */
    for (p = 0; p < sizeof passos / sizeof passos[0]; p++)
        for (fase = 0; fase < 96; fase++) {
            uint32_t a, b, cnt = (fase & 1u) ? 5040u - fase : fase * 50u;
            passo = passos[p];
            ps2_pad_ligado[1] = (uint8_t)(fase % 3u == 0u);   /* controle 2 ligado em 1/3 */
            cpu_tiques = fase % 12u;                    /* 0 a ~370 ciclos entre as rotinas */
            chamadas += fase;                           /* muda a fase do ACK e do contador */
            /* como o jogo deixa o timer 2 no VBlank: /8, volta no alvo, sem IRQ */
            ps2_io_write(T2_ALVO, 5040u, 2);
            ps2_io_write(T2_MODO, 0x248u, 2);
            ps2_io_write(T2_CNT, cnt, 2);               /* perto da volta em metade dos casos */
            a = vblank(0);
            ps2_io_write(T2_CNT, cnt, 2);
            b = vblank(1);
            /* sem adianto: os dois prazos de 430 unidades de 8 ciclos, no mínimo */
            assert(a >= 2u * 430u * 8u);
            real += a; adiantado += b; n++;
        }
    printf("pad_espera_test: OK (%u casos; ciclos de PS1 por VBlank: %lu reais, %lu com adianto)\n",
           n, (unsigned long)(real / n), (unsigned long)(adiantado / n));
    assert(adiantado * 2u < real);                      /* o adianto tem de valer a pena */
    return 0;
}
