/*
 * io.h — registradores de hardware do PS1 (0x1F801000-0x1F802FFF).
 *
 * Fase 2: implementação mínima para o jogo avançar na inicialização.
 *  - Interrupções: I_STAT (0x1F801070) / I_MASK (0x1F801074)
 *  - Timers 0-2 (0x1F801100-0x1F80112F), contados pelo relógio do EE
 *  - DMA (0x1F801080-0x1F8010F7): conclusão instantânea; canal 1
 *    escreve zeros, canal 3 lê CD e canal 6 monta a ordering table
 *  - MDEC: status pronto e dados descartados, sem decodificação
 *  - GPU: comandos GP0/GP1 e DMA2 no módulo gpu.c
 *  - SPU: apenas registradores mínimos, sem reprodução
 *  - O resto: registrador genérico (guarda o valor) + log do 1º acesso
 */
#ifndef PS2_IO_H
#define PS2_IO_H

#include <stdint.h>

/* Bits de I_STAT/I_MASK */
#define IRQ_VBLANK 0
#define IRQ_GPU    1
#define IRQ_CDROM  2
#define IRQ_DMA    3
#define IRQ_TMR0   4
#define IRQ_TMR1   5
#define IRQ_TMR2   6
#define IRQ_PAD    7
#define IRQ_SIO    8
#define IRQ_SPU    9

void     ps2_io_init(void);
int      ps2_io_hit(uint32_t address);          /* 1 se é I/O tratado aqui */
uint32_t ps2_io_read(uint32_t address, unsigned width);
void     ps2_io_write(uint32_t address, uint32_t value, unsigned width);

void     ps2_irq_raise(unsigned bit);
void     ps2_irq_ack(unsigned bit);
uint32_t ps2_irq_pending(void);                /* I_STAT & I_MASK */

void     ps2_timers_poll(void);                /* IRQs dos timers */
void     ps2_dma_poll(void);                   /* V48: fim atrasado do DMA1 */
/* V35: eventos gerados / IRQs entregues / descartadas desde a última chamada (zera) */
unsigned long ps2_timers_escritas(int t);      /* V37: escritas desde a última chamada */
void     ps2_timers_estat(int t, unsigned long *eventos, unsigned long *entregues,
                          unsigned long *descartadas, unsigned *modo, unsigned *alvo);

/* Relógio do PS1 (33,8688 MHz) derivado do contador do EE. */
uint32_t ps2_ps1_cycles(void);

/* V56: liga/desliga o adiantamento das esperas da libpad (io.c) */
void ps2_timer_pad_adianta(int ligar);

#endif
