/*
 * abertura.h — telas de início do port (V59), as mesmas dos ports do
 * Super Mario World e do Yu-Gi-Oh! para PS2: a vinheta da Retrogames
 * (partículas que formam um cubo dourado girando, com um acorde
 * sintetizado aqui), o título do port e a tela "Apoie o projeto" com o QR
 * do Pix. Cada tela abre e fecha em círculo.
 *
 * Chame depois de iniciar o vídeo, o som e os controles, antes do jogo.
 * START pula a vinheta e o título; na tela de apoio, X ou START (ou 20 s)
 * seguem para o jogo. Sem controle, segue sozinha.
 *
 * Os quadros são 256 x 224 (0x00RRGGBB) desenhados aqui, com fundo e
 * letras próprios (fonte 8x8 do PS2SDK): nada do jogo nem da Namco.
 */
#ifndef PS2_ABERTURA_H
#define PS2_ABERTURA_H

#include <stdint.h>

enum { ABERTURA_W = 256, ABERTURA_H = 224 };

void ps2_abertura(const char *versao);

/* Desenho das telas (separado para o teste no PC: tests/abertura_test.c). */
void abertura_vinheta(uint32_t *px, int quadro);
void abertura_titulo(uint32_t *px, const char *versao);
void abertura_apoio(uint32_t *px, int quadro, int pode_seguir);
void abertura_iris(uint32_t *px, int r);
/* Buffer W x H para guardar o fundo pronto (NULL = sem cache). */
void abertura_cache(uint32_t *buf);
int  abertura_iris_raio(int q, int abrindo);
enum { ABERTURA_VINHETA_TRAVA = 90, ABERTURA_IRIS_MAX = 187, ABERTURA_IRIS_QUADROS = 24 };

#endif
