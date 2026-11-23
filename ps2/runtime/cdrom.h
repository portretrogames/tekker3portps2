/*
 * cdrom.h — controlador de CD-ROM do PS1 (0x1F801800-0x1F801803).
 *
 * Lê o disco de uma imagem .bin (setores de 2352 bytes) e responde aos
 * comandos da libcd da Sony com as interrupções (INT1..INT5) certas.
 * XA e CD-DA tocam em som.c (V47 e V59).
 */
#ifndef PS2_CDROM_H
#define PS2_CDROM_H

#include <stdint.h>

/* Faixas do disco (Redump): .bin de cada uma / 2352. A 2 e a 3 começam com
 * 2 s de pregap (INDEX 00) dentro do próprio arquivo. */
#define PS2_CD_T1_SETORES 268934u
#define PS2_CD_T2_SETORES 11778u
#define PS2_CD_T3_SETORES 11923u

/* V59 (som.c): o CD-DA parou sozinho. normal = 1: fim da faixa (com
 * Autopause, INT4 = DataEnd); 0: erro de leitura (só para). */
void     ps2_cdrom_cdda_fim(int normal);

/* Procura a imagem do disco (ver cdrom.c) a partir da pasta do ELF. */
int      ps2_cdrom_abrir(const char *pasta_elf);
void     ps2_cdrom_reset(void);
int      ps2_cdrom_ler_dados(uint32_t lba, uint8_t *dst2048);  /* ISO9660 */
uint8_t  ps2_cdrom_read(uint32_t reg);           /* reg = 0..3 */
void     ps2_cdrom_write(uint32_t reg, uint8_t v);
void     ps2_cdrom_update(uint32_t ciclos_ps1);  /* chamado periodicamente */
void     ps2_cdrom_dma(uint32_t madr, uint32_t palavras);   /* DMA canal 3 */

#endif
