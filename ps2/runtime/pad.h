/*
 * pad.h — porta de controle/memory card do PS1 (SIO0, 0x1F801040-0x1F80104F).
 *
 * Simula um controle digital (SCPH-1080) na porta 1 e, com o controle 2 do
 * PS2 ligado, na porta 2 (V59); memory cards: desconectados. Os botões vêm
 * de ps2_pad_botoes (ativo em 0, formato do PS1), preenchido por video_gs.c
 * a cada VBlank com os controles do PS2 (kit).
 */
#ifndef PS2_PAD_H
#define PS2_PAD_H

#include <stdint.h>

extern uint16_t ps2_pad_botoes[2];   /* 0xFFFF = nada apertado */
extern uint8_t  ps2_pad_ligado[2];   /* 1 = porta responde (a 1 sempre) */

void     ps2_pad_reset(void);
void     ps2_pad_tick(void);     /* entrega a IRQ de ACK adiada */
int ps2_pad_ack_ocupado(void);       /* io.c: esperas da libpad (ver pad.c) */
uint32_t ps2_pad_read(uint32_t off, unsigned w);    /* off = 0x40..0x4F */
void     ps2_pad_write(uint32_t off, uint32_t v, unsigned w);

#endif
