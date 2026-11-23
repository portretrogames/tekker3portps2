/*
 * quente.h — lugar fixo, no cache de dados do EE, do que o desenho usa o tempo
 * todo (V56).
 *
 * O cache de dados do EE tem 8 KB em 2 vias: 64 conjuntos de 64 bytes, e o
 * conjunto de um dado é o endereço dele módulo 4 KB. Três dados usados em
 * alternância no mesmo conjunto se expulsam a cada uso. Sem cuidado, quem cai
 * com quem muda a cada compilação (o tamanho do código desloca todos os dados)
 * e com a profundidade da pilha.
 *
 * Medido no console (make PMC=1 PMC_VARRE=1, 21/11/2026), só descendo a pilha
 * da lista de desenho de 256 em 256 bytes: de 6 a 18 faltas de cache de dados
 * por polígono, 910 a 1220 ciclos por polígono (a lista inteira: ~3,4 a
 * ~4,5 ms por quadro). Entre duas compilações com o mesmo código da lista:
 * 7,7 e 15,1 faltas por polígono.
 *
 * Por isso:
 *  - PS2_QUENTE: as variáveis fixas do caminho de cada polígono (gpu.c,
 *    gs_render.c) vão para a seção .bss.quente, que o linkfile do ps2sdk junta
 *    depois de todo o .bss comum: ficam coladas, uma linha de cache cada, logo
 *    depois do bloco alinhado em 4 KB de ram.c (g_ps2_quente). Conjuntos 0 em
 *    diante, sempre os mesmos.
 *    (Só entra variável que começa em zero: a seção é .bss.)
 *  - PS2_PILHA_EM(alvo): no laço de despacho do jogo, no começo da lista de
 *    desenho e de cada rotina nativa a pilha do EE desce até o endereço ficar
 *    = alvo (mod 4 KB). Os quadros de pilha dali para baixo caem sempre nos
 *    mesmos conjuntos.
 *  - A janela da RAM do PS1 e o estado da CPU: ver ram.h.
 */
#ifndef PS2_QUENTE_H
#define PS2_QUENTE_H
#include <stdint.h>

#if defined(_EE)
#define PS2_QUENTE __attribute__((section(".bss.quente")))
#define PS2_PILHA_EM(alvo) do { \
        uintptr_t ps2_sp_; void *ps2_pn_; \
        __asm__ volatile("move %0, $sp" : "=r"(ps2_sp_)); \
        ps2_pn_ = __builtin_alloca((ps2_sp_ - (uintptr_t)(alvo)) & 4095u); \
        __asm__ volatile("" : : "r"(ps2_pn_)); } while (0)
#else
#define PS2_QUENTE
#define PS2_PILHA_EM(alvo) do { } while (0)
#endif

/*
 * Posição da pilha (mod 4 KB) em cada caminho, escolhida pela varredura no
 * console (V56, faltas de cache de dados por polígono):
 *   lista    0 a 1536 e 2560 a 3584: 6,6 a 6,8;  2048: 11,9
 *   nativas  0: 6,2  512: 6,5  1024: 5,9  1536: 10,6  2048: 7,7  2560: 6,2
 *            3072: 7,0  3584: 6,3
 * Ficam no meio de um trecho bom, longe da posição ruim.
 */
#ifndef PS2_PILHA_LISTA
#define PS2_PILHA_LISTA   0xC00u
#endif
#ifndef PS2_PILHA_NATIVAS
#define PS2_PILHA_NATIVAS 0xA00u
#endif
#ifndef PS2_PILHA_JOGO
#define PS2_PILHA_JOGO    0xE00u
#endif

#endif
