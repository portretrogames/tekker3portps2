/* Listas do DMA2 com ciclo (base: reprodução do ChatGPT, V11). Sem assets.
 * Stubs contam palavras enviadas à GPU. Exige: cada nó uma vez, DMA concluído,
 * novo disparo também limitado. */
#include <assert.h>
#include "../runtime/io.c"

uint8_t g_ps2_janela[PS2_JANELA_TAM];
uint32_t g_cod_ini, g_cod_tam;
uint8_t g_cod_sujo[4096];
uint8_t g_cod_pag[PS2_RAM_SIZE >> PS2_COD_PAG_BITS];
void ps2_cod_marcar_lento(uint32_t addr) { (void)addr; }
uint32_t g_ps2_vram_geracao;
static unsigned long palavras;
void ps2_gpu_gp0(uint32_t v) { (void)v; palavras++; }
void ps2_gpu_gp0_bloco(const uint32_t *p, uint32_t n) { (void)p; palavras += n; }
uint32_t ps2_gpu_read(void) { return 0; }
void ps2_mdec_dma_in(uint32_t a, uint32_t n) { (void)a; (void)n; assert(0); }
int ps2_mdec_dma_out(uint32_t a, uint32_t n) { (void)a; (void)n; assert(0); return 0; }
void ps2_cdrom_dma(uint32_t a, uint32_t n) { (void)a; (void)n; assert(0); }

static unsigned long disparar(uint32_t madr)
{
    palavras = 0;
    reg_set(0xA0u, madr, 4);
    dma_chcr_write(0xA8u, 0x01000401u);
    assert(!(reg_get(0xA8u, 4) & (1u << 24)));      /* concluído */
    return palavras;
}

int main(void)
{
    /* válida: 1 nó com 1 comando */
    memset(g_ps1_ram, 0, PS2_RAM_SIZE);
    ps2_ram_set32(0x1000, 0x01FFFFFFu); ps2_ram_set32(0x1004, 0xE1000000u);
    assert(disparar(0x1000) == 1 && g_ps2_listas_ciclicas == 0);

    /* válida longa: 3000 nós encadeados */
    {
        uint32_t i;
        memset(g_ps1_ram, 0, PS2_RAM_SIZE);
        for (i = 0; i < 3000; i++) {
            uint32_t a = 0x10000u + i * 8u;
            ps2_ram_set32(a, i == 2999 ? 0x01FFFFFFu : (0x01000000u | (a + 8u)));
            ps2_ram_set32(a + 4, 0xE1000000u);
        }
        assert(disparar(0x10000) == 3000 && g_ps2_listas_ciclicas == 0);
    }

    /* autociclo: o nó vai uma vez, DMA conclui; novo disparo idem */
    memset(g_ps1_ram, 0, PS2_RAM_SIZE);
    ps2_ram_set32(0x1000, 0x01001000u); ps2_ram_set32(0x1004, 0xE1000000u);
    assert(disparar(0x1000) == 1 && g_ps2_listas_ciclicas == 1);
    assert(disparar(0x1000) == 1 && g_ps2_listas_ciclicas == 2);

    /* prefixo de 2 nós + ciclo de 3 nós: 5 nós, 2 palavras cada */
    {
        static const uint32_t nos[5] = {0x2000, 0x2100, 0x2200, 0x2300, 0x2400};
        static const uint32_t prox[5] = {0x2100, 0x2200, 0x2300, 0x2400, 0x2200};
        unsigned i;
        memset(g_ps1_ram, 0, PS2_RAM_SIZE);
        for (i = 0; i < 5; i++) {
            ps2_ram_set32(nos[i], 0x02000000u | prox[i]);
            ps2_ram_set32(nos[i] + 4, 0xE1000000u); ps2_ram_set32(nos[i] + 8, 0xE2000000u);
        }
        assert(disparar(0x2000) == 10 && g_ps2_listas_ciclicas == 3);
    }
    puts("dma_ciclo_test: OK");
    return 0;
}
