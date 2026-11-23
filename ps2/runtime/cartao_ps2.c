/*
 * cartao_ps2.c — onde o memory card do PS1 imitado (cartao_ps1.c) fica no
 * PS2 (V59): no memory card do PS2 da porta 1, pasta TEKKEN3PS2, com o
 * icon.sys e o ícone 3D (cartao_icone.h, tools/gerar_icone.py) e a imagem
 * do cartão do PS1 em TEKKEN3.MCR (128 KB, o mesmo formato dos .mcr dos
 * emuladores).
 *
 * Na partida: lê a imagem (ou começa com um cartão formatado, se ainda não
 * há). Durante o jogo: depois que o jogo grava algo e fica 1 s sem gravar,
 * manda uma cópia para o memory card do PS2 em etapas (kit_cartao_passo, um
 * pedaço por VBlank: o jogo não para). Sem memory card do PS2 na porta 1,
 * o jogo vê a porta do cartão vazia.
 */
#include "cartao_ps1.h"
#include "cartao_icone.h"
#include "kit/cartao.h"

#include <stdio.h>
#include <string.h>

#define PASTA "TEKKEN3PS2"
#define IMAGEM "TEKKEN3.MCR"
#define QUIETO_VB 60u
#define TENTATIVAS 3

static uint8_t copia[MC_TAM] __attribute__((aligned(64)));
static uint8_t icone_lido[sizeof(kT3IconeSys)] __attribute__((aligned(64)));
static unsigned salvo, visto, quieto;
static int precisa_icone, falhas, gravando;
static int recuperar;   /* a imagem veio de ~TEKKEN3.MCR: regravar TEKKEN3.MCR direto */

void ps2_cartao_iniciar(void)
{
    int r;
    g_mc_presente = 0;
    if (!kit_cartao_iniciar() || !kit_cartao_tem(0)) {
        return;
    }
    r = kit_cartao_ler(0, "/" PASTA "/" IMAGEM, g_mc_imagem, MC_TAM);
    if (r != MC_TAM || !mc_valida(g_mc_imagem)) {
        /* sem o arquivo, curto ou estranho: a gravação pode ter parado
         * depois de ~TEKKEN3.MCR (completo) ficar pronto (kit/cartao.h) */
        int r2 = kit_cartao_ler(0, "/" PASTA "/~" IMAGEM, g_mc_imagem, MC_TAM);
        if (r2 == MC_TAM && mc_valida(g_mc_imagem)) {
            recuperar = 1;
            r = MC_TAM;
        } else if (r == -4 && (r2 == -4 || (r2 >= 0 && r2 < MC_TAM))) {
            r = -4;     /* nada, ou só o 1º save interrompido no meio: cartão novo */
        } else {
            r = -1;     /* algo ilegível: não mexe */
        }
    }
    if (r == -4) {
        mc_formatar(g_mc_imagem);
    } else if (r != MC_TAM) {
        /* erro de leitura ou imagem estranha: não arrisca gravar por cima de
         * um save que pode estar bom; nesta partida o jogo vê a porta vazia */
        return;
    }
    /* ícone ausente ou de outra versão do desenho: grava junto do próximo save */
    r = kit_cartao_ler(0, "/" PASTA "/icon.sys", icone_lido, (int)sizeof(icone_lido));
    precisa_icone = r != (int)sizeof(kT3IconeSys) || memcmp(icone_lido, kT3IconeSys, sizeof(kT3IconeSys)) != 0;
    g_mc_presente = 1;
    salvo = visto = g_mc_mudou;
    if (recuperar) salvo--;     /* grava logo, direto em TEKKEN3.MCR (ver ps2_cartao_vblank) */
}

/* Uma vez por VBlank (video_gs.c). */
void ps2_cartao_vblank(void)
{
    kit_cartao_passo();
    if (!g_mc_presente) return;
    if (gravando && !kit_cartao_ocupado()) {
        gravando = 0;
        if (kit_cartao_resultado() == 1) {
            precisa_icone = 0; falhas = 0;
        } else if (++falhas < TENTATIVAS) {
            salvo--;                    /* tenta de novo depois */
        }
    }
    if (g_mc_mudou != visto) { visto = g_mc_mudou; quieto = 0; return; }
    if (g_mc_mudou == salvo || gravando || (++quieto < QUIETO_VB && !recuperar)) return;
    {
        kit_cartao_arquivo a[3];
        int n = 0;
        memset(a, 0, sizeof(a));
        memcpy(copia, g_mc_imagem, MC_TAM);
        if (precisa_icone) {
            a[n].nome = "icon.sys"; a[n].dados = kT3IconeSys; a[n].tam = (int)sizeof(kT3IconeSys); n++;
            a[n].nome = T3_ICONE_NOME; a[n].dados = kT3Icone; a[n].tam = (int)sizeof(kT3Icone); n++;
        }
        /* recuperação: ~TEKKEN3.MCR é a única cópia boa; TEKKEN3.MCR é
         * gravado direto, sem passar por ela */
        a[n].nome = IMAGEM; a[n].dados = copia; a[n].tam = MC_TAM; a[n].direto = recuperar; n++;
        if (kit_cartao_gravar(0, PASTA, a, n)) { gravando = 1; salvo = g_mc_mudou; recuperar = 0; }
    }
}
