/*
 * kit/diagnostico.h — log em arquivo e medição de tempo (módulo
 * "diagnóstico" do kit).
 *
 * Log: depois de kit_log_iniciar(), o printf escreve num buffer na RAM
 * (stream próprio via fopencookie). kit_log_gravar() abre o arquivo,
 * acrescenta o buffer e FECHA, conferindo cada passo. Não depende de
 * freopen nem de o arquivo ficar aberto (no FAT/exFAT o tamanho só é
 * gravado no fechamento). Cada gravação trava ~50 ms no pendrive do PS2
 * (abre/escreve/fecha no FAT): chamar kit_log_gravar() o menos possível.
 *
 * Tempo: COUNT do EE (294.912.000 ciclos por segundo no PS2).
 */
#ifndef KIT_DIAGNOSTICO_H
#define KIT_DIAGNOSTICO_H

#include <stdint.h>
#include <stddef.h>

/* Liga o log em 'caminho' (o arquivo é recriado vazio). 1 = ok. */
int  kit_log_iniciar(const char *caminho);
/* Sem log: descarta tudo que for para o stdout (não chama o IOP). */
void kit_log_silenciar(void);
/* Grava o que está no buffer. 1 = ok ou nada a gravar. */
int  kit_log_gravar(void);
/* Texto curto para um painel: "LOG OK 12345" / "LOG ERRO ABRIR -5" / "LOG DESLIGADO". */
const char *kit_log_estado(void);
/* Função chamada antes de cada gravação (ex.: terminar uma leitura
 * assíncrona que usa o mesmo fileXio). NULL desliga. */
void kit_log_antes_de_gravar(void (*f)(void));
/* Bytes no buffer esperando gravação (0 se o log está desligado) e o
 * tamanho do buffer: para gravar menos vezes sem estourar. */
size_t kit_log_pendente(void);
size_t kit_log_capacidade(void);

#define KIT_CICLOS_POR_MS 294912u

static inline uint32_t kit_relogio(void)
{
#ifdef _EE
    uint32_t c;
    __asm__ volatile("mfc0 %0, $9" : "=r"(c));
    return c;
#else
    return 0;
#endif
}
/* Milissegundos desde 'inicio' (válido para intervalos < 14 s). */
static inline uint32_t kit_ms_desde(uint32_t inicio)
{
    return (uint32_t)(kit_relogio() - inicio) / KIT_CICLOS_POR_MS;
}

#endif
