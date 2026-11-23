/*
 * tests/stubs_pc.c — símbolos do runtime que os testes do PC usam mas não
 * compilam junto (make test).
 *
 *   g_ps2_tmr_servindo  timers cuja callback está rodando (bios_hle.c, V43);
 *                       lido por io.c (timer_write).
 *   g_cod_ver           versão da validade do código (codigo.c, V31); usado
 *                       pelo caminho rápido de escrita na RAM (ram.h).
 *
 * As definições são fracas (weak): quando o teste já compila o arquivo de
 * verdade (ex.: revisao_test com bios_hle.c, memory_test com codigo.c), vale
 * o de verdade.
 */
#include <stdint.h>
#include "ram.h"            /* declara g_cod_ver: o compilador confere o tipo */

/* Sem header (o runtime declara com extern na hora de usar): mesma
 * assinatura de bios_hle.c. */
extern unsigned g_ps2_tmr_servindo;

__attribute__((weak)) unsigned g_ps2_tmr_servindo = 0;

/* V47: levantamento do som (runtime/som.c), chamado por io.c e cdrom.c */
#include "../runtime/som.h"
__attribute__((weak)) void ps2_som_spu_escrita(uint32_t off, uint32_t v, unsigned w) { (void)off; (void)v; (void)w; }
__attribute__((weak)) void ps2_som_dma4(uint32_t madr, uint32_t bcr, uint32_t chcr) { (void)madr; (void)bcr; (void)chcr; }
__attribute__((weak)) int ps2_som_spu_leitura(uint32_t off, unsigned w, uint32_t *v)
{ (void)w; if (off == 0xD9C) { *v = 0xFFFFu; return 1; } if (off == 0xD9E) { *v = 0xFFu; return 1; } return 0; }
__attribute__((weak)) void ps2_som_cd_cmd(uint8_t cmd, const uint8_t *param, uint32_t lba) { (void)cmd; (void)param; (void)lba; }
__attribute__((weak)) void ps2_som_cdda_abrir(const char *pasta) { (void)pasta; }
__attribute__((weak)) uint32_t ps2_som_cdda_lba(void) { return 0; }
__attribute__((weak)) void ps2_som_xa(const uint8_t *setor, int xa_ligado) { (void)setor; (void)xa_ligado; }
__attribute__((weak)) void ps2_som_vblank(void) {}
/* V48: imagem em bloco para a VRAM; 0 = o io.c segue palavra por palavra */
__attribute__((weak)) unsigned ps2_gpu_gp0_imagem(const void *p, unsigned n) { (void)p; (void)n; return 0; }
__attribute__((weak)) int ps2_som_ocioso(void) { return 0; }
__attribute__((weak)) int ps2_som_pendente(void) { return 0; }
__attribute__((weak)) int g_ps2_contagem_irq = 1;   /* stubs.c (bios_hle.c usa desde a V47) */
__attribute__((weak)) uint32_t g_cod_ver = 1;
