/*
 * jogo.h — tudo o que é específico do Tekken 3 e que as bibliotecas do kit
 * (../kit) NÃO sabem: nomes de arquivos, pastas e a versão mostrada.
 */
#ifndef JOGO_H
#define JOGO_H

/* JOGO_SUFIXO: variante de compilação (ex.: " Os"), vem do Makefile */
#ifndef JOGO_SUFIXO
#define JOGO_SUFIXO ""
#endif
#define JOGO_VERSAO_NUM  "V60"
#define JOGO_VERSAO      "T3 PS2 " JOGO_VERSAO_NUM JOGO_SUFIXO
#define JOGO_PASTA_USB   "mass:APPS/TEKKEN3/"     /* reserva: pasta padrão do OPL (V53) */
/* V53: o log leva a versão no nome (tekken3_log_V53.txt); variantes de teste
 * acrescentam uma marca (make LOG_MARCA=_diag) */
#ifndef JOGO_LOG_MARCA
#define JOGO_LOG_MARCA   ""
#endif
#define JOGO_LOG         "tekken3_log_" JOGO_VERSAO_NUM JOGO_LOG_MARCA ".txt"
/* Log: grava no pendrive a cada N relatórios de ~2 s (cada gravação trava
 * ~50 ms no console). 0 = log desligado. Vem do Makefile (LOG_BLOCOS). */
#ifndef PS2_LOG_BLOCOS
#define PS2_LOG_BLOCOS 15
#endif
#define JOGO_EXE         "SLUS_004.02"
/* tratador de VBlank da libpad do jogo (conversa com os controles): io.c
 * adianta as esperas dele (ps2_timer_pad_adianta) */
#define JOGO_PAD_VBLANK  0x80092D34u
/* arquivos que indicam que o pendrive montou (procurados ao lado do ELF) */
#define JOGO_ARQUIVOS    "TEKKEN3.BIN", "Tekken 3 (USA) (Track 1).bin", JOGO_EXE

#endif
