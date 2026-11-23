/*
 * Regressão dos achados F1, F4, F5 e F6 da revisão técnica (14/11/2026,
 * reproduções originais feitas pelo ChatGPT/Codex). Relógio controlado:
 * compile com -Dclock=relogio_teste.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../runtime/memory.h"
#include "../runtime/io.h"
#include "../runtime/ram.h"
#include "../runtime/gpu.h"
#include "../runtime/bios_hle.h"

/* Mesmo padrão de runtime/io.c (fila das IRQs do timer, V35). */

static clock_t ticks;
clock_t relogio_teste(void) { return ticks; }
void ps2_video_vblank(void) {}

static void chamar(CPUState *c, unsigned tab, unsigned fn, unsigned a0)
{
    c->gpr[9] = fn; c->gpr[4] = a0; c->gpr[31] = 0x80010000u;
    ps2_bios_call(c, tab);
}

int main(void)
{
    CPUState c;
    unsigned h;
    memset(&c, 0, sizeof c);
    ps2_memory_init(&c);

    /* F1: GPU_cw (A0 49h) chega na GPU igual ao GP0 direto */
    chamar(&c, 0xA0, 0x49, 0x020000FFu);
    chamar(&c, 0xA0, 0x49, 0x00010000u);
    chamar(&c, 0xA0, 0x49, 0x00010010u);
    assert(ps2_gpu_vram()[1024] == 0x001Fu);

    /* F5: alvo com IRQ e sem reset no alvo */
    ps2_io_write(0x1F801074u, 1u << IRQ_TMR0, 4);
    ps2_io_write(0x1F801104u, 0x50u, 4);
    ps2_io_write(0x1F801108u, 1000u, 4);
    ticks = 60;                                 /* ~2032 ciclos */
    ps2_timers_poll();
    assert(ps2_irq_pending() & (1u << IRQ_TMR0));

    /* V19 1.2: zerar o contador depois de cruzar o alvo não gera IRQ falsa */
    ps2_io_init();
    ticks = 0;
    ps2_io_write(0x1F801074u, 1u << IRQ_TMR0, 4);
    ps2_io_write(0x1F801104u, 0x58u, 4);        /* reset no alvo, IRQ no alvo, repetir */
    ps2_io_write(0x1F801108u, 1000u, 4);
    ticks = 60;                                 /* ~2032 ciclos: alvo cruzado 2 vezes */
    ps2_timers_poll();
    assert(ps2_irq_pending() & (1u << IRQ_TMR0));
    ps2_io_write(0x1F801070u, ~(1u << IRQ_TMR0), 4);
    /* V35 (PS2_TMR_RECUPERAR): o 2º cruzamento ficou na fila e sai assim que
     * o jogo reconhece o 1º. Não é IRQ falsa: reconhece e segue o teste. */
    ps2_timers_poll();                          /* mesmo instante */
    assert(ps2_irq_pending() & (1u << IRQ_TMR0));
    ps2_io_write(0x1F801070u, ~(1u << IRQ_TMR0), 4);
    ps2_io_write(0x1F801100u, 0u, 4);           /* COUNT = 0 */
    ps2_timers_poll();                          /* mesmo instante */
    assert(!(ps2_irq_pending() & (1u << IRQ_TMR0)));
    ps2_io_write(0x1F801108u, 2000u, 4);        /* aumentar o alvo também não */
    ps2_timers_poll();
    assert(!(ps2_irq_pending() & (1u << IRQ_TMR0)));
    ticks = 130;                                /* ~2370 ciclos depois: cruza 2000 */
    ps2_timers_poll();
    assert(ps2_irq_pending() & (1u << IRQ_TMR0));

    /* F6: escrever DICR com o bit mestre forçado gera IRQ de DMA */
    ps2_io_init();
    ps2_io_write(0x1F801074u, 1u << IRQ_DMA, 4);
    ps2_io_write(0x1F8010F4u, 1u << 15, 4);
    assert(ps2_irq_pending() & (1u << IRQ_DMA));

    /* V48: DICR em 8 e 16 bits. O libcd liga/desliga a IRQ do DMA3 escrevendo
     * só o byte 2 (1F8010F6); antes isso caía num registrador qualquer e o
     * vídeo da abertura travava na 1a perda de setor. */
    ps2_io_init();
    ps2_io_write(0x1F801074u, 1u << IRQ_DMA, 4);
    ps2_io_write(0x1F8010F4u, 0x00880000u, 4);          /* mestre + canal 3 */
    assert(ps2_io_read(0x1F8010F6u, 1) == 0x88u);
    ps2_io_write(0x1F8010F6u, 0x80u, 1);                 /* desliga o canal 3 */
    assert((ps2_io_read(0x1F8010F4u, 4) & 0x00FF0000u) == 0x00800000u);
    ps2_io_write(0x1F8010B0u, 0x80020000u, 4);           /* DMA3 (CD), sem IRQ */
    ps2_io_write(0x1F8010B4u, 0x00010001u, 4);
    ps2_io_write(0x1F8010B8u, 0x11000000u, 4);
    assert(!(ps2_io_read(0x1F8010F4u, 4) & (1u << 27)));
    assert(!(ps2_irq_pending() & (1u << IRQ_DMA)));
    ps2_io_write(0x1F8010F6u, 0x88u, 1);                 /* liga o canal 3 */
    ps2_io_write(0x1F8010B8u, 0x11000000u, 4);           /* agora avisa */
    assert(ps2_io_read(0x1F8010F4u, 4) & (1u << 27));
    assert(ps2_irq_pending() & (1u << IRQ_DMA));
    ps2_io_write(0x1F8010F6u, 0x80u, 1);                 /* byte 2 não reconhece o flag */
    assert(ps2_io_read(0x1F8010F4u, 4) & (1u << 27));
    ps2_io_write(0x1F8010F7u, 0x08u, 1);                 /* byte 3: escrever 1 limpa */
    assert(!(ps2_io_read(0x1F8010F4u, 4) & (1u << 27)));
    ps2_io_write(0x1F8010F6u, 0x0088u, 2);               /* 16 bits: enables + flags(0) */
    assert((ps2_io_read(0x1F8010F4u, 4) & 0x7FFF0000u) == 0x00880000u);
    assert(ps2_io_read(0x1F8010F5u, 2) == 0x8800u);

    /* V48: o fim do DMA1 (MDEC -> RAM) avisa depois, numa conferência
     * seguinte, e não dentro da escrita do CHCR (o tratador de IRQ do jogo
     * fazia as 16 faixas do quadro de uma vez com as interrupções desligadas) */
    ps2_io_init();
    ticks = 0;
    ps2_io_write(0x1F801074u, 1u << IRQ_DMA, 4);
    ps2_io_write(0x1F8010F4u, 0x00820000u, 4);          /* mestre + canal 1 */
    {   /* um macrobloco de 24 bits (6 blocos só com DC) = 192 palavras */
        unsigned i;
        ps2_io_write(0x1F801820u, 1u << 29 | 2u << 27 | 6u, 4);
        for (i = 0; i < 6u; i++) ps2_io_write(0x1F801820u, 0xFE000400u, 4);
    }
    ps2_io_write(0x1F801090u, 0x80010000u, 4);
    ps2_io_write(0x1F801094u, 0x00060020u, 4);           /* 6 x 32 palavras */
    ps2_io_write(0x1F801098u, 0x01000200u, 4);
    assert(ps2_io_read(0x1F801098u, 4) & (1u << 24));    /* ainda ocupado */
    assert(!(ps2_irq_pending() & (1u << IRQ_DMA)));
    ps2_dma_poll();                                       /* mesmo instante */
    assert(ps2_io_read(0x1F801098u, 4) & (1u << 24));
    ticks = 20;                                           /* ~680 ciclos */
    ps2_dma_poll();
    assert(!(ps2_io_read(0x1F801098u, 4) & (1u << 24)));
    assert(ps2_io_read(0x1F8010F4u, 4) & (1u << 25));
    assert(ps2_irq_pending() & (1u << IRQ_DMA));
    assert(ps2_io_read(0x1F801824u, 4) & (1u << 31));    /* saída do MDEC consumida */

    /* ... e na ordem inversa (DMA1 armado antes de o MDEC ter os dados): o
     * aviso também espera o tempo das 192 palavras (revisão V48) */
    ps2_io_write(0x1F8010F4u, 0x00820000u | (1u << 25), 4);   /* limpa o flag */
    ps2_irq_ack(IRQ_DMA);
    assert(!(ps2_irq_pending() & (1u << IRQ_DMA)));
    ps2_io_write(0x1F801090u, 0x80010000u, 4);
    ps2_io_write(0x1F801094u, 0x00060020u, 4);
    ps2_io_write(0x1F801098u, 0x01000200u, 4);           /* ainda sem dados */
    assert(ps2_io_read(0x1F801098u, 4) & (1u << 24));
    {
        unsigned i;
        ps2_io_write(0x1F801820u, 1u << 29 | 2u << 27 | 6u, 4);
        for (i = 0; i < 6u; i++) ps2_io_write(0x1F801820u, 0xFE000400u, 4);
    }
    ps2_dma_poll();                                       /* mesmo instante */
    assert(ps2_io_read(0x1F801098u, 4) & (1u << 24));
    assert(!(ps2_irq_pending() & (1u << IRQ_DMA)));
    ticks = 40;                                           /* +~680 ciclos */
    ps2_dma_poll();
    assert(!(ps2_io_read(0x1F801098u, 4) & (1u << 24)));
    assert(ps2_irq_pending() & (1u << IRQ_DMA));

    /* ... ler o CHCR não conclui (o tratador da libetc relê em laço e faria
     * as 16 faixas numa IRQ só); a escrita do modo de um timer conclui,
     * como antes de o fim do DMA1 sair de ps2_timers_poll */
    ps2_io_write(0x1F8010F4u, 0x00820000u | (1u << 25), 4);
    ps2_irq_ack(IRQ_DMA);
    {
        unsigned i;
        ps2_io_write(0x1F801820u, 1u << 29 | 2u << 27 | 6u, 4);
        for (i = 0; i < 6u; i++) ps2_io_write(0x1F801820u, 0xFE000400u, 4);
    }
    ps2_io_write(0x1F801098u, 0x01000200u, 4);
    assert(ps2_io_read(0x1F801098u, 4) & (1u << 24));
    ticks = 60;
    assert(ps2_io_read(0x1F801098u, 4) & (1u << 24));
    ps2_io_write(0x1F801104u, 0u, 4);                    /* modo do timer 0 */
    assert(!(ps2_io_read(0x1F801098u, 4) & (1u << 24)));
    assert(ps2_io_read(0x1F8010F4u, 4) & (1u << 25));

    /* F4: WaitEvent de evento ainda não entregue não retorna */
    c.gpr[5] = 2; c.gpr[6] = 0x2000; c.gpr[7] = 0;
    chamar(&c, 0xB0, 0x08, 0xF2000000u);
    h = c.gpr[2];
    chamar(&c, 0xB0, 0x0C, h);
    chamar(&c, 0xB0, 0x0A, h);
    assert(c.pc == 0xB0u);                      /* vai repetir a chamada */

    puts("Revisao (F1, F4, F5, F6, V19 1.2, V48 DICR em bytes e fim do DMA1): OK");
    return 0;
}
