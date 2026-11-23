/*
 * cartao_ps1.h — memory card do PS1 imitado (V59), para as chamadas da BIOS
 * (bios_hle.c): uma imagem de 128 KB (16 blocos de 8 KB, 1024 quadros de
 * 128 bytes) com o sistema de arquivos do PS1 — o bloco 0 é o diretório
 * (quadro 0 "MC", quadros 1-15 uma entrada por bloco de dados, 16-35 lista
 * de setores ruins, 63 cópia do 0). Referência: nocash psx-spx, "Memory
 * Card Data Format" e "BIOS Memory Card Functions".
 *
 * A imagem é a mesma de um .mcr/.mcd de emulador. No PS2 ela fica no
 * memory card do PS2 (cartao_ps2 em main.c/video_gs.c); aqui só a lógica,
 * testada no PC (tests/cartao_test.c).
 */
#ifndef PS2_CARTAO_PS1_H
#define PS2_CARTAO_PS1_H

#include <stdint.h>

enum {
    MC_TAM = 128 * 1024, MC_BLOCO = 8192, MC_QUADRO = 128, MC_BLOCOS = 15,
    MC_NOME = 21,           /* nome do arquivo: até 20 letras + 0 */
    MC_FDS = 4              /* arquivos abertos ao mesmo tempo */
};

/* Modos do open() da BIOS */
enum { MC_FREAD = 0x0001, MC_FWRITE = 0x0002, MC_FCREAT = 0x0200, MC_FASYNC = 0x8000 };

extern uint8_t g_mc_imagem[MC_TAM];
extern int     g_mc_presente;   /* 0 = sem cartão (o jogo vê a porta vazia) */
extern unsigned g_mc_mudou;     /* conta gravações (quem salva no PS2 compara) */

void mc_formatar(uint8_t *img);
void mc_formatar_cartao(void);              /* format() da BIOS: fecha os arquivos e formata */
int  mc_valida(const uint8_t *img);         /* 1 = imagem com "MC" e diretório coerente */

/* Arquivos (nome sem o "bu00:"). Devolvem como a BIOS: fd >= 0 ou -1. */
int  mc_abrir(const char *nome, uint32_t modo);
int  mc_ler(int fd, uint8_t *dst, uint32_t n);          /* bytes lidos, -1 erro */
int  mc_escrever(int fd, const uint8_t *src, uint32_t n);
int  mc_posicionar(int fd, int32_t pos, int de);        /* de: 0 início, 1 atual; nova posição ou -1 */
int  mc_fechar(int fd);
int  mc_apagar(const char *nome);                       /* 1 = apagou */
int  mc_modo(int fd);                                   /* modo do open, -1 se fechado */

/* Procura (firstfile/nextfile): padrão com '?' e '*'. Devolve 1 e preenche
 * a entrada da BIOS (0x28 bytes: nome[20], attr, tamanho, próximo, bloco
 * inicial, sistema) ou 0 quando acabou. */
int  mc_primeiro(const char *padrao, uint8_t entrada[0x28]);
int  mc_proximo(uint8_t entrada[0x28]);

/* Quadro de 128 bytes (read/write_card_sector). 1 = ok. */
int  mc_quadro_ler(unsigned quadro, uint8_t dst[MC_QUADRO]);
int  mc_quadro_escrever(unsigned quadro, const uint8_t src[MC_QUADRO]);

#endif
