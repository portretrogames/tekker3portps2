/* Exemplo mínimo do kit: prepara o armazenamento, liga o log e mostra os
 * botões do controle no log por ~10 s. Sem nenhum asset de jogo. */
#include <stdio.h>
#include "kit/armazenamento.h"
#include "kit/controle.h"
#include "kit/diagnostico.h"

int main(int argc, char *argv[])
{
    char pasta[200] = "mass:/", log[256];
    const char *argv0 = argc > 0 ? argv[0] : NULL;
    kit_controle_estado e;
    uint32_t ini;
    int i;

    kit_arm_pasta_do_elf(argv0, pasta, sizeof(pasta));
    kit_arm_preparar(argv0, 0);
    snprintf(log, sizeof(log), "%sexemplo_log.txt", pasta);
    kit_log_iniciar(log);
    fputs(kit_arm_mensagens(), stdout);
    kit_controle_iniciar(1);

    for (i = 0; i < 10; i++) {
        ini = kit_relogio();
        while (kit_ms_desde(ini) < 1000u) {}
        kit_controle_ler(0, &e);
        printf("segundo %d: conectado=%d botoes=%04x\n", i, e.conectado, e.botoes);
        kit_log_gravar();
    }
    printf("%s\n", kit_log_estado());
    kit_log_gravar();
    return 0;
}
