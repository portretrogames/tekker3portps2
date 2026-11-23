/*
 * kit/som.h — saída de som PCM pelo audsrv (módulo "som" do kit).
 *
 * Carrega no IOP o freesd (PS2SDK) e o audsrv do kit (kit/iop/audsrv: o do
 * PS2SDK com o volume do PCM corrigido, vindo do port SMW PS2), os dois
 * EMBUTIDOS no ELF. O som é sempre 16 bits estéreo intercalado (L, R).
 *
 * O IOP toca o buffer circular dele sem parar: se o jogo não mandar nada
 * ele repete o que sobrou (no SMW PS2 um bip ficava "apitando"). Quem usa o
 * kit manda silêncio quando não tiver som (kit_som_completar).
 */
#ifndef KIT_SOM_H
#define KIT_SOM_H

#include <stdint.h>

/* Carrega os módulos e configura freq Hz, 16 bits, estéreo, volume máximo.
 * Chame depois de kit_arm_preparar(). Devolve 1 se o som ficou pronto. */
int kit_som_iniciar(int freq);

/* 1 depois de kit_som_iniciar() com sucesso. */
int kit_som_pronto(void);

/* Cada pergunta ao IOP é uma chamada RPC: o EE fica parado até o IOP
 * responder. Por isso só kit_som_livre() pergunta; as outras usam o valor
 * que ela trouxe. Chame kit_som_livre() uma vez por quadro, antes delas. */

/* Pergunta ao IOP quantos quadros (L+R) cabem agora na fila dele. */
unsigned kit_som_livre(void);

/* Quadros na fila do IOP, pelo último kit_som_livre() e o que foi enviado. */
unsigned kit_som_na_fila(void);

/* Manda até 'quadros' quadros (o que couber pelo último kit_som_livre());
 * nunca espera. Devolve quantos foram. */
unsigned kit_som_enviar(const int16_t *pcm, unsigned quadros);

/* Manda silêncio até a fila do IOP ter 'alvo' quadros (usa o último
 * kit_som_livre()). */
void kit_som_completar(unsigned alvo);

/* Envio SEM ESPERAR (0.2.0): o EE não fica parado esperando o IOP. Um envio
 * por vez; o IOP aceita o que couber na fila dele e o que não couber é
 * descartado lá (mande de novo depois). Uso, uma vez por quadro:
 *   n = kit_som_concluir();       -1: o envio anterior ainda não voltou
 *                                  (não mande nada agora); senão quantos
 *                                  quadros dele o IOP aceitou (0 se nenhum)
 *   kit_som_mandar(a, qa, b, qb); copia qa quadros de 'a' e qb de 'b' (para
 *                                  buffer circular que dá a volta) e manda
 * Os dados podem ser reaproveitados assim que kit_som_mandar() volta.
 * Não misture com kit_som_enviar()/kit_som_completar() com um envio em voo. */
#define KIT_SOM_LOTE 1024u   /* quadros por envio, no máximo */
int kit_som_concluir(void);
unsigned kit_som_mandar(const int16_t *a, unsigned qa, const int16_t *b, unsigned qb);
/* Igual a kit_som_mandar, com 'quadros' de silêncio. */
unsigned kit_som_mandar_silencio(unsigned quadros);

/* Texto curto do estado ("audsrv 48000 Hz, fila 20480 bytes" ou o erro). */
const char *kit_som_estado(void);

/* SPU do PS1 tocada na SPU2 (0.3.0): pacotes de comandos (formato em
 * kit/iop/audsrv/src/ps1spu.c) por um canal RPC próprio, sem esperar. Um
 * pacote por vez; a resposta traz o ENDX e o ENVX das 24 vozes depois dele.
 * Depende de kit_som_iniciar(). Usa o canal 0 de transferência da SPU2, o
 * mesmo do ADPCM do audsrv (audsrv_load_adpcm...): não use os dois juntos. */
#define KIT_SPU1_PALAVRAS 4096u        /* palavras de 32 bits por pacote (16 KB) */
typedef struct {
    uint32_t endx;                     /* bit n = voz n terminou */
    uint16_t envx[24];                 /* envelope atual de cada voz */
    uint32_t pacotes;                  /* pacotes tratados pelo IOP */
    uint32_t erros;                    /* blocos de amostras que a SPU2 recusou (total) */
} kit_spu1_resposta;
int kit_spu1_pronto(void);
/* 1 = o pacote anterior ainda não voltou */
int kit_spu1_ocupado(void);
/* Manda p[0..palavras) (1 a KIT_SPU1_PALAVRAS; mais que isso é recusado).
 * 0 se ocupado, sem som ou tamanho inválido. */
int kit_spu1_enviar(const uint32_t *p, unsigned palavras);
/* Espera o pacote em voo voltar (até ~1 s). */
void kit_spu1_esperar(void);
/* Copia a resposta mais nova; 1 se chegou uma desde a última chamada. */
int kit_spu1_resposta_nova(kit_spu1_resposta *r);

#endif
