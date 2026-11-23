/*
 * som.h — interface de runtime/som.c (V47: música XA do CD).
 */
#ifndef PS2_SOM_H
#define PS2_SOM_H
#include <stdint.h>

/* io.c: escrita em 0x1F801C00-0x1F801FFF (off = endereço - 0x1F801000) e
 * DMA do canal 4 (RAM -> SPU) */
void ps2_som_spu_escrita(uint32_t off, uint32_t v, unsigned w);
void ps2_som_dma4(uint32_t madr, uint32_t bcr, uint32_t chcr);
/* io.c: leitura que a SPU2 responde (ENDX, ENVX); 1 = tratou (V53) */
int ps2_som_spu_leitura(uint32_t off, unsigned w, uint32_t *v);

/* cdrom.c: comando do CD e setor de áudio XA lido */
void ps2_som_cd_cmd(uint8_t cmd, const uint8_t *param, uint32_t lba);
/* V59: CD-DA. Abre a faixa 2 na pasta do disco (na partida: o fileXio sem
 * pedido em andamento); LBA atual do Play (GetlocP). */
void ps2_som_cdda_abrir(const char *pasta);
uint32_t ps2_som_cdda_lba(void);
void ps2_som_xa(const uint8_t *setor, int xa_ligado);

/* bios_hle.c: uma vez por VBlank do PS1; espera do quadro (1 se há setor na
 * fila; decodifica 1 grupo, devolve 1 se havia o que fazer) */
void ps2_som_vblank(void);
int ps2_som_pendente(void);
int ps2_som_ocioso(void);

/* video_gs.c: linhas "som:" e "som saida:" do log (~2 s) */
void ps2_som_relatorio(void);

#endif
