/* Regressão do achado F2: setor XA entre dois setores de dados. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../runtime/cdrom.c"
void ps2_irq_raise(unsigned bit) { (void)bit; }
int main(void)
{
    unsigned char s[2352] = {0};
    disco = tmpfile();
    fwrite(s, 1, sizeof s, disco); s[18] = 4;
    fwrite(s, 1, sizeof s, disco); s[18] = 0;
    fwrite(s, 1, sizeof s, disco); fflush(disco);
    ps2_cdrom_reset(); modo = 0x40; lendo = 1; prox_setor = 0; agora = 0;
    ps2_cdrom_update(0); preparar_fifo(); fifo_pos = fifo_n; iflag = 0;
    ps2_cdrom_update(CICLOS_SETOR_1X);           /* setor XA */
    ps2_cdrom_update(CICLOS_SETOR_1X * 2u);      /* deve seguir para o 3º */
    assert(atual_lba == 3u);
    fclose(disco);

    /* V48 (revisão): recuperando atraso, um setor XA não abre caminho para
     * o próximo setor sair na mesma conferência (mínimo de meio período) */
    disco = tmpfile();
    s[18] = 0; fwrite(s, 1, sizeof s, disco);
    s[18] = 4; fwrite(s, 1, sizeof s, disco);
    s[18] = 0; fwrite(s, 1, sizeof s, disco);
    fwrite(s, 1, sizeof s, disco); fflush(disco);
    agora = 0; ps2_cdrom_reset(); modo = 0x40; lendo = 1; prox_setor = 0;
    ps2_cdrom_update(0); preparar_fifo(); fifo_pos = fifo_n; iflag = 0;
    assert(atual_lba == 1u);
    ps2_cdrom_update(CICLOS_SETOR_1X * 4u);      /* atrasado: lê o XA */
    assert(atual_lba == 2u);
    ps2_cdrom_update(CICLOS_SETOR_1X * 4u);      /* mesmo instante: espera */
    assert(atual_lba == 2u);
    ps2_cdrom_update(CICLOS_SETOR_1X * 4u + CICLOS_SETOR_1X / 2u);
    assert(atual_lba == 3u);
    fclose(disco);
    puts("Revisao (F2, V48 XA na recuperacao de atraso): OK");
    return 0;
}
