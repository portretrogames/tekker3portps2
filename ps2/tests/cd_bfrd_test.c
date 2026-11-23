/* Pedido BFRD x chegada de setores (base: reprodução do ChatGPT, V9).
 * Setores sintéticos, sem dados do jogo. Agora exige PROGRESSO. */
#include <assert.h>
#include "../runtime/cdrom.c"
uint8_t g_ps2_janela[PS2_JANELA_TAM];
uint32_t g_cod_ini, g_cod_tam;
uint8_t g_cod_sujo[4096];
uint8_t g_cod_pag[PS2_RAM_SIZE >> PS2_COD_PAG_BITS];
void ps2_cod_marcar_lento(uint32_t addr) { (void)addr; }
void ps2_irq_raise(unsigned bit) { (void)bit; }

static FILE *disco_sintetico(const uint8_t *marcas, const uint8_t *xa, unsigned n)
{
    FILE *f = tmpfile();
    unsigned i;
    assert(f);
    for (i = 0; i < n; i++) {
        uint8_t s[SETOR] = {0};
        s[15] = 2; s[18] = xa[i] ? 4 : 0; s[24] = marcas[i];
        assert(fwrite(s, 1, SETOR, f) == SETOR);
    }
    assert(fflush(f) == 0);
    return f;
}

int main(void)
{
    /* 1: BFRD antes do setor -> atendido quando o setor de dados chega */
    {
        const uint8_t m[1] = {0xA5}, x[1] = {0};
        disco = disco_sintetico(m, x, 1);
        ps2_cdrom_reset(); agora = 0; comecar_leitura();
        ps2_cdrom_write(0, 0);
        ps2_cdrom_write(3, 0x80);
        assert(!(ps2_cdrom_read(0) & 0x40));
        ps2_cdrom_update(prox_setor);
        assert(setor_ok && iflag == INT1);
        assert(ps2_cdrom_read(0) & 0x40);
        assert(ps2_cdrom_read(2) == 0xA5);
        fclose(disco);
    }
    /* 2: dados -> ACK -> atraso -> XA -> BFRD: XA não apaga o dado pendente */
    {
        const uint8_t m[3] = {0x11, 0x22, 0x33}, x[3] = {0, 1, 0};
        disco = disco_sintetico(m, x, 3);
        ps2_cdrom_reset(); modo = 0x40; agora = 0; comecar_leitura();
        ps2_cdrom_update(prox_setor);
        assert(iflag == INT1 && setor_ok && setor_lba == 0u);
        ps2_cdrom_write(0, 1); ps2_cdrom_write(3, 0x1F); ps2_cdrom_write(0, 0);
        ps2_cdrom_update(agora + 8u * periodo_setor());     /* passa o XA */
        assert(setor_ok && setor_lba == 0u);
        ps2_cdrom_write(3, 0x80);
        assert(ps2_cdrom_read(0) & 0x40);
        assert(ps2_cdrom_read(2) == 0x11);
        fclose(disco);
    }
    /* 3: BFRD=0 cancela o pedido pendente */
    {
        const uint8_t m[1] = {0x77}, x[1] = {0};
        disco = disco_sintetico(m, x, 1);
        ps2_cdrom_reset(); agora = 0; comecar_leitura();
        ps2_cdrom_write(0, 0);
        ps2_cdrom_write(3, 0x80);
        ps2_cdrom_write(3, 0x00);
        ps2_cdrom_update(prox_setor);
        assert(setor_ok && !(ps2_cdrom_read(0) & 0x40));
        fclose(disco);
    }
    /* 4: leitura sequencial de 40 setores: conteúdo certo, 2 freads, 1 fseek */
    {
        uint8_t m[40], x[40] = {0};
        unsigned i;
        for (i = 0; i < 40; i++) m[i] = (uint8_t)(0x40 + i);
        disco = disco_sintetico(m, x, 40);
        ps2_cdrom_reset(); agora = 0; comecar_leitura();
        g_ps2_cd_leituras_arquivo = g_ps2_cd_seeks = 0;
        for (i = 0; i < 40; i++) {
            ps2_cdrom_update(prox_setor);
            assert(iflag == INT1 && setor_lba == i);
            ps2_cdrom_write(0, 0); ps2_cdrom_write(3, 0x80);
            assert(ps2_cdrom_read(2) == m[i]);
            ps2_cdrom_write(0, 1); ps2_cdrom_write(3, 0x1F); ps2_cdrom_write(0, 0);
            ps2_cdrom_write(3, 0x00);
        }
        assert(g_ps2_cd_leituras_arquivo == 2 && g_ps2_cd_seeks == 1);
        fclose(disco);
    }
    puts("cd_bfrd_test: OK");
    return 0;
}
