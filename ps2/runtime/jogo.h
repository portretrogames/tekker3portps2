/*
 * jogo.h — tudo o que é específico do Tekken 3 e que as bibliotecas do kit
 * (../kit) NÃO sabem: nomes de arquivos, pastas e a versão mostrada.
 */
#ifndef JOGO_H
#define JOGO_H

#define JOGO_VERSAO_NUM  "V60"
#define JOGO_VERSAO      "T3 PS2 " JOGO_VERSAO_NUM
#define JOGO_PASTA_USB   "mass:APPS/TEKKEN3/"     /* reserva: pasta padrão do OPL (V53) */
#define JOGO_EXE         "SLUS_004.02"
/* tratador de VBlank da libpad do jogo (conversa com os controles): io.c
 * adianta as esperas dele (ps2_timer_pad_adianta) */
#define JOGO_PAD_VBLANK  0x80092D34u
/* arquivos que indicam que o pendrive montou (procurados ao lado do ELF) */
#define JOGO_ARQUIVOS    "TEKKEN3.BIN", "Tekken 3 (USA) (Track 1).bin", JOGO_EXE

#endif
