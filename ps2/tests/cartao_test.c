/*
 * Memory card do PS1 imitado (runtime/cartao_ps1.c, V59): formato do
 * diretório, criar/escrever/ler/procurar/apagar como a BIOS, quadros soltos.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../runtime/cartao_ps1.h"

static uint8_t xor7f(const uint8_t *q) { uint8_t x = 0; int i; for (i = 0; i < 0x7F; i++) x ^= q[i]; return x; }

int main(void)
{
    static uint8_t buf[3 * MC_BLOCO], volta[3 * MC_BLOCO];
    uint8_t ent[0x28], q[MC_QUADRO];
    unsigned i, mudou;
    int fd, fd2;

    g_mc_presente = 1;
    mc_formatar(g_mc_imagem);
    assert(mc_valida(g_mc_imagem));
    assert(g_mc_imagem[0] == 'M' && g_mc_imagem[1] == 'C' && g_mc_imagem[0x7F] == ('M' ^ 'C'));
    for (i = 1; i < 36; i++) assert(xor7f(g_mc_imagem + i * MC_QUADRO) == g_mc_imagem[i * MC_QUADRO + 0x7F]);
    assert(g_mc_imagem[MC_QUADRO] == 0xA0);
    assert(!memcmp(g_mc_imagem, g_mc_imagem + 63 * MC_QUADRO, MC_QUADRO));

    /* sem o arquivo: open de leitura falha (o Tekken começa assim) */
    assert(mc_abrir("BASLUS-00402TEKKEN-3", MC_FREAD) == -1);
    assert(!mc_primeiro("BASLUS*", ent));

    /* criar com 2 blocos, escrever 2 blocos, ler de volta */
    mudou = g_mc_mudou;
    fd = mc_abrir("BASLUS-00402TEKKEN-3", MC_FCREAT | MC_FWRITE | (2u << 16));
    assert(fd >= 0 && g_mc_mudou != mudou);
    assert(mc_abrir("BASLUS-00402TEKKEN-3", MC_FCREAT | (1u << 16)) == -1);   /* já existe */
    for (i = 0; i < sizeof(buf); i++) buf[i] = (uint8_t)(i * 7 + 3);
    assert(mc_escrever(fd, buf, 100) == -1);                    /* só múltiplos de 128 */
    assert(mc_escrever(fd, buf, 2 * MC_BLOCO) == 2 * MC_BLOCO);
    assert(mc_escrever(fd, buf, MC_QUADRO) == 0);               /* passou do fim */
    assert(mc_fechar(fd) == fd);
    fd = mc_abrir("BASLUS-00402TEKKEN-3", MC_FREAD);
    assert(fd >= 0);
    assert(mc_posicionar(fd, MC_BLOCO, 0) == MC_BLOCO);
    assert(mc_ler(fd, volta, MC_BLOCO) == MC_BLOCO);
    assert(!memcmp(volta, buf + MC_BLOCO, MC_BLOCO));
    assert(mc_posicionar(fd, 0, 0) == 0);
    assert(mc_ler(fd, volta, 2 * MC_BLOCO) == 2 * MC_BLOCO && !memcmp(volta, buf, 2 * MC_BLOCO));
    mc_fechar(fd);

    /* diretório: 1º bloco 51h com tamanho e nome, 2º 53h, encadeados */
    assert(g_mc_imagem[1 * MC_QUADRO] == 0x51 && g_mc_imagem[2 * MC_QUADRO] == 0x53);
    assert(g_mc_imagem[1 * MC_QUADRO + 4] == 0x00 && g_mc_imagem[1 * MC_QUADRO + 5] == 0x40);   /* 16384 */
    assert(g_mc_imagem[1 * MC_QUADRO + 8] == 1 && g_mc_imagem[2 * MC_QUADRO + 8] == 0xFF);
    assert(!strcmp((char *)g_mc_imagem + MC_QUADRO + 0x0A, "BASLUS-00402TEKKEN-3"));
    assert(xor7f(g_mc_imagem + MC_QUADRO) == g_mc_imagem[MC_QUADRO + 0x7F]);
    assert(mc_valida(g_mc_imagem));

    /* procura */
    fd2 = mc_abrir("OUTRO", MC_FCREAT | (1u << 16));
    assert(fd2 >= 0);
    mc_fechar(fd2);
    assert(mc_valida(g_mc_imagem));       /* arquivo de 1 bloco: 51h com próximo FFFFh (console, V59) */
    assert(mc_primeiro("BASLUS-00402????????", ent) && !strncmp((char *)ent, "BASLUS-00402TEKKEN-3", 20));
    assert(ent[0x18] == 0x00 && ent[0x19] == 0x40 && ent[0x20] == 1);
    assert(!mc_proximo(ent));
    assert(mc_primeiro("*", ent) && mc_proximo(ent) && !mc_proximo(ent));

    {   /* imagens com cadeia inválida são recusadas */
        static uint8_t ruim[MC_TAM];
        memcpy(ruim, g_mc_imagem, MC_TAM);
        ruim[2 * MC_QUADRO] = 0x52;             /* bloco 2 vira do meio e aponta para o 1: ciclo */
        ruim[2 * MC_QUADRO + 8] = 0; ruim[2 * MC_QUADRO + 9] = 0;
        assert(!mc_valida(ruim));
        memcpy(ruim, g_mc_imagem, MC_TAM);
        ruim[5 * MC_QUADRO] = 0x53;             /* 53h solto */
        assert(!mc_valida(ruim));
        memcpy(ruim, g_mc_imagem, MC_TAM);
        ruim[3 * MC_QUADRO + 8] = 1;            /* OUTRO (1 bloco) passa a usar o bloco 2 de outro */
        ruim[3 * MC_QUADRO + 9] = 0;
        assert(!mc_valida(ruim));
    }
    fd = mc_abrir("BASLUS-00402TEKKEN-3", MC_FREAD);
    assert(fd >= 0);

    /* apagar libera os blocos para outro arquivo (e invalida o fd aberto) */
    assert(mc_apagar("BASLUS-00402TEKKEN-3") && !mc_apagar("BASLUS-00402TEKKEN-3"));
    assert(g_mc_imagem[MC_QUADRO] == 0xA1 && g_mc_imagem[2 * MC_QUADRO] == 0xA3);
    assert(mc_ler(fd, volta, MC_QUADRO) == -1 && mc_fechar(fd) == -1);
    fd = mc_abrir("GRANDE", MC_FCREAT | (14u << 16));
    assert(fd >= 0);                                           /* 14 = 15 - OUTRO */
    mc_fechar(fd);
    assert(mc_abrir("MAIS", MC_FCREAT | (1u << 16)) == -1);   /* cheio */

    /* quadros soltos (write_card_sector 0x3F do Tekken) */
    memset(q, 0x5A, sizeof(q));
    assert(mc_quadro_escrever(0x3F, q) && mc_quadro_ler(0x3F, volta) && !memcmp(volta, q, MC_QUADRO));
    assert(!mc_quadro_ler(1024, volta));

    /* formatar invalida os arquivos abertos */
    fd = mc_abrir("OUTRO", MC_FREAD);
    assert(fd >= 0);
    mc_formatar_cartao();
    assert(mc_ler(fd, volta, MC_QUADRO) == -1 && mc_abrir("OUTRO", MC_FREAD) == -1);

    /* sem cartão: tudo falha */
    g_mc_presente = 0;
    assert(mc_abrir("OUTRO", MC_FREAD) == -1 && !mc_primeiro("*", ent) && !mc_quadro_ler(0, volta));
    printf("cartao_test: OK\n");
    return 0;
}
