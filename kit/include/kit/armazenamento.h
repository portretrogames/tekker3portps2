/*
 * kit/armazenamento.h — preparar o acesso a arquivos no PS2 (módulo
 * "armazenamento" do kit).
 *
 * Dois caminhos:
 *  - host: (PCSX2): o IOP é mantido, nada é carregado;
 *  - USB (console via OPL/wLaunchELF): reinicia o IOP e carrega os módulos
 *    EMBUTIDOS no ELF (iomanX, fileXio, usbd, bdm, bdmfs_fatfs = FAT32/exFAT,
 *    usbmass_bd). Depois fopen/fread da libc passam pelo fileXio.
 *
 * O kit não sabe quais arquivos o jogo usa: quem chama monta a lista de
 * caminhos e usa kit_arm_esperar() para aguardar o pendrive montar.
 * O Makefile do projeto inclui kit/kit.mk (gera os IRX com bin2c).
 */
#ifndef KIT_ARMAZENAMENTO_H
#define KIT_ARMAZENAMENTO_H

#include <stddef.h>

enum {
    KIT_ARM_OK = 0,
    KIT_ARM_HOST = 1,          /* rodando de host: — nada foi feito */
    KIT_ARM_ERRO_MODULO = -1,  /* algum IRX não carregou */
    KIT_ARM_ERRO_FILEXIO = -2, /* fileXioInit falhou */
    KIT_ARM_NAO_ACHOU = -3     /* kit_arm_esperar: nenhum arquivo apareceu */
};

/* argv0 = caminho do ELF. forcar_usb = 1 faz o caminho do console mesmo
 * em host: (teste no PCSX2). Devolve KIT_ARM_OK, KIT_ARM_HOST ou erro. */
int kit_arm_preparar(const char *argv0, int forcar_usb);

/* Espera até timeout_ms que algum dos caminhos (lista terminada em NULL)
 * possa ser aberto. Copia o encontrado em 'achado' (se não for NULL).
 * Devolve KIT_ARM_OK ou KIT_ARM_NAO_ACHOU. */
int kit_arm_esperar(const char *const *caminhos, unsigned timeout_ms,
                    char *achado, size_t tam);

/* Pasta do ELF a partir do argv0 ("mass0:APPS/JOGO/x.elf" -> "mass0:APPS/JOGO/").
 * Devolve 1 se conseguiu. */
int kit_arm_pasta_do_elf(const char *argv0, char *saida, size_t tam);

/* 1 se o fileXio foi carregado por kit_arm_preparar (leitura assíncrona
 * possível com ps2sdk_get_iop_fd + fileXio NOWAIT). */
int kit_arm_filexio_ativo(void);

/* Mensagens do início (antes do log abrir), para copiar para o log. */
const char *kit_arm_mensagens(void);

#endif
