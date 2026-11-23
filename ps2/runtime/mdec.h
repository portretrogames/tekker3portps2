/*
 * mdec.h — decodificador de vídeo do PS1 (MDEC). Referência: nocash psx-spx
 * ("Macroblock Decoder"). Comandos: 1 = decodificar macroblocos,
 * 2 = tabelas de quantização, 3 = tabela de escala (IDCT).
 *
 * A decodificação é feita de uma vez quando o último parâmetro chega; a
 * saída fica numa fila lida pelo DMA1 (ou pela porta de dados).
 */
#ifndef PS2_MDEC_H
#define PS2_MDEC_H

#include <stdint.h>

void     ps2_mdec_reset(void);
uint32_t ps2_mdec_read_data(void);
uint32_t ps2_mdec_read_status(void);
void     ps2_mdec_write_data(uint32_t value);
void     ps2_mdec_write_control(uint32_t value);

/* DMA0 (RAM -> MDEC) e DMA1 (MDEC -> RAM). madr = endereço PS1. */
void     ps2_mdec_dma_in(uint32_t madr, uint32_t palavras);
/* 1 = concluído; 0 = pendente (completa quando houver dados e então chama
 * ps2_dma_mdec_saida_pronta(palavras entregues depois), definida em io.c). */
int      ps2_mdec_dma_out(uint32_t madr, uint32_t palavras);
void     ps2_dma_mdec_saida_pronta(uint32_t palavras);

#endif
