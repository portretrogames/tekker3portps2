/*
 * spu.h — SPU do PS1 tocada na SPU2 do PS2 (runtime/spu.c, V53).
 */
#ifndef PS2_SPU_H
#define PS2_SPU_H
#include <stdint.h>

/* som.c: escrita em 0x1F801C00-0x1F801FFF (off = endereço - 0x1F801000) */
void ps2_spu_escrita(uint32_t off, uint32_t v, unsigned w);
/* som.c: leitura de registrador que a SPU2 responde (ENDX, ENVX); 1 = tratou */
int ps2_spu_leitura(uint32_t off, unsigned w, uint32_t *v);
/* som.c: DMA 4 (bit 0 do CHCR: 1 = RAM -> SPU) */
void ps2_spu_dma4(uint32_t madr, uint32_t bcr, uint32_t chcr);
/* som.c: uma vez por VBlank do PS1 */
void ps2_spu_vblank(void);

#endif
