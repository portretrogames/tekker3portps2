/* kit/src/controle.c — ver kit/controle.h. Base: ps2sdk samples/rpc/pad. */
#include "kit/controle.h"

#include <stdio.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <libpad.h>

static char buf_porta[2][256] __attribute__((aligned(64)));
static int  aberta[2];
static int  estado_ant[2] = { -1, -1 };
static int  modo_pedido[2];         /* 0.3.3: modo analógico já pedido (ou desnecessário) nesta conexão */
static int  modo_tentativas[2];     /* pedidos recusados pela libpad nesta conexão */

/* 0.3.3: põe um DualShock no modo analógico, sem trava, uma vez por conexão.
 * Só com o controle estável e sem pedido em andamento (padSetMainMode é
 * assíncrono). Controle sem modo DualShock (digital) fica como está. */
static void pedir_analogico(int porta)
{
    int i, n;
    if (modo_pedido[porta]) return;
    if (padGetReqState(porta, 0) == PAD_RSTAT_BUSY) return;
    n = padInfoMode(porta, 0, PAD_MODETABLE, -1);
    for (i = 0; i < n; i++)
        if (padInfoMode(porta, 0, PAD_MODETABLE, i) == PAD_TYPE_DUALSHOCK) {
            if (padInfoMode(porta, 0, PAD_MODECURID, 0) == PAD_TYPE_DUALSHOCK) break;
            if (padSetMainMode(porta, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_UNLOCK) == 1) {
                printf("controle: porta %d: modo analogico pedido\n", porta + 1);
                break;
            }
            /* recusado: tenta de novo nas próximas leituras, até 3 vezes */
            if (++modo_tentativas[porta] < 3) return;
            printf("controle: porta %d: modo analogico recusado\n", porta + 1);
            break;
        }
    modo_pedido[porta] = 1;     /* pedido, já analógico, recusado 3x ou sem DualShock */
}

int kit_controle_iniciar(int portas)
{
    /* 0.2.1: carregar SIO2MAN/PADMAN e chamar padInit de novo trava o libpad:
     * cada etapa é marcada só depois de dar certo, e uma chamada seguinte só
     * abre as portas pedidas que ainda estão fechadas */
    static int sio2man_ok, padman_ok, pad_ok;
    int r, p, n = 0;
    if (portas < 1) portas = 1;
    if (portas > 2) portas = 2;
    if (!pad_ok) {
        sceSifInitRpc(0);
        if (!sio2man_ok) {
            r = SifLoadModule("rom0:SIO2MAN", 0, NULL);
            if (r < 0) { printf("controle: SIO2MAN falhou (%d)\n", r); return 0; }
            sio2man_ok = 1;
        }
        if (!padman_ok) {
            r = SifLoadModule("rom0:PADMAN", 0, NULL);
            if (r < 0) { printf("controle: PADMAN falhou (%d)\n", r); return 0; }
            padman_ok = 1;
        }
        r = padInit(0);
        if (r < 0) { printf("controle: padInit falhou (%d)\n", r); return 0; }
        pad_ok = 1;
    }
    for (p = 0; p < portas; p++) {
        if (aberta[p]) { n++; continue; }
        if (padPortOpen(p, 0, buf_porta[p]) == 0) {
            printf("controle: porta %d nao abriu\n", p + 1);
            continue;
        }
        aberta[p] = 1;
        n++;
        printf("controle: porta %d aberta (libpad)\n", p + 1);
    }
    return n;
}

int kit_controle_ler(int porta, kit_controle_estado *e)
{
    struct padButtonStatus st;
    int estado;

    e->conectado = 0;
    e->botoes = 0xFFFFu;
    e->analogico = 0;
    e->lx = e->ly = e->rx = e->ry = 128u;
    if (porta < 0 || porta > 1 || !aberta[porta]) return 0;
    estado = padGetState(porta, 0);
    if (estado != estado_ant[porta]) {
        estado_ant[porta] = estado;
        printf("controle: porta %d estado %d\n", porta + 1, estado);
    }
    /* outro controle depois (EXECCMD não: é o próprio pedido de modo) */
    if (estado == PAD_STATE_DISCONN || estado == PAD_STATE_FINDPAD || estado == PAD_STATE_ERROR)
        modo_pedido[porta] = modo_tentativas[porta] = 0;
    if (estado != PAD_STATE_STABLE && estado != PAD_STATE_FINDCTP1) return 0;
    if (estado == PAD_STATE_STABLE) pedir_analogico(porta);
    if (padRead(porta, 0, &st) == 0) return 0;
    e->conectado = 1;
    e->botoes = st.btns;
    if ((st.mode >> 4) == 0x7u) {       /* 0x73 analógico, 0x79 com pressão */
        e->analogico = 1;
        e->lx = st.ljoy_h; e->ly = st.ljoy_v;
        e->rx = st.rjoy_h; e->ry = st.rjoy_v;
    }
    return 1;
}
