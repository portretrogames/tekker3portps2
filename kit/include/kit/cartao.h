/*
 * kit/cartao.h — memory card do PS2 (módulo "cartao" do kit, 0.3.3).
 *
 * Carrega rom0:MCMAN e rom0:MCSERV (presentes em toda BIOS de PS2; combinam
 * com o rom0:SIO2MAN/PADMAN do módulo "controle", que tem de ser iniciado
 * antes) e usa a libmc. Escrita SEM TRAVAR o jogo: kit_cartao_gravar() só
 * prepara; kit_cartao_passo(), chamado uma vez por quadro, anda uma etapa
 * (criar a pasta, abrir, gravar um pedaço, fechar...) quando a anterior
 * terminou, sem nunca esperar. Cada arquivo é gravado inteiro como "~NOME"
 * e depois NOME é apagado e gravado de novo: uma gravação interrompida
 * deixa NOME antigo inteiro, ou "~NOME" novo inteiro (quem lê tenta NOME e,
 * se faltar ou vier curto, "~NOME"); no fim "~NOME" é apagado. O prefixo
 * "~" é do kit: nomes com "/" ou começando com "~" são recusados, e um
 * "~NOME" que já esteja na pasta é apagado. Os nomes são copiados; os DADOS
 * têm de ficar intactos até kit_cartao_ocupado() == 0 (sem exigência de
 * alinhamento).
 *
 * Leitura e kit_cartao_tem são síncronas e recusadas (erro / 0) enquanto
 * houver gravação em andamento: a libmc atende um pedido por vez.
 */
#ifndef KIT_CARTAO_H
#define KIT_CARTAO_H

#include <stdint.h>

/* Carrega os módulos e chama mcInit. 1 = pronto. */
int kit_cartao_iniciar(void);

/* 1 se há memory card de PS2 formatado na porta (0 ou 1). */
int kit_cartao_tem(int porta);

/* Lê "/PASTA/ARQ" inteiro (até cap bytes). Devolve os bytes lidos,
 * -4 se o arquivo não existe, outro negativo em erro. */
int kit_cartao_ler(int porta, const char *caminho, void *buf, int cap);

typedef struct {
    const char *nome;       /* só o nome, dentro da pasta */
    const void *dados;      /* tam > 0 bytes */
    int tam;
    int direto;             /* 1: grava NOME direto, sem passar por ~NOME
                             * (recuperação: quando ~NOME é a cópia boa) */
} kit_cartao_arquivo;

/* Prepara a gravação de n arquivos (até 4) na pasta (sem ou com "/" na
 * frente; criada se faltar). 0 se já há gravação em andamento ou algum
 * argumento é inválido (nome vazio ou maior que 30 letras, tam <= 0...). */
int kit_cartao_gravar(int porta, const char *pasta, const kit_cartao_arquivo *arqs, int n);

/* Anda a gravação (uma vez por quadro). Nunca espera. */
void kit_cartao_passo(void);

int kit_cartao_ocupado(void);
/* Resultado da última gravação: 1 ok, 0 nenhuma terminou ainda, < 0 erro
 * (o código da libmc). */
int kit_cartao_resultado(void);

#endif
