/*
 * Telas de início (runtime/abertura.c, V59) no PC: desenha a vinheta (em
 * vários momentos), o título e a tela de apoio, confere tamanho e alguns
 * pontos, e grava cada tela em build/tests/abertura_*.ppm para olhar.
 *
 * A fonte msx (2 KB) vem da libdebug do PS2SDK: o Makefile extrai os bytes
 * para build/tests/msx_fonte.h (tools/msx_fonte.py).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../runtime/abertura.h"
#include "msx_fonte.h"          /* unsigned char msx[2048] */

static uint32_t px[ABERTURA_W * ABERTURA_H];

static void grava(const char *nome)
{
    char caminho[128];
    FILE *f;
    int i;
    snprintf(caminho, sizeof(caminho), "build/tests/abertura_%s.ppm", nome);
    f = fopen(caminho, "wb");
    assert(f);
    assert(fprintf(f, "P6\n%d %d\n255\n", ABERTURA_W, ABERTURA_H) > 0);
    for (i = 0; i < ABERTURA_W * ABERTURA_H; i++) {
        unsigned char c[3] = { (unsigned char)(px[i] >> 16), (unsigned char)(px[i] >> 8), (unsigned char)px[i] };
        assert(fwrite(c, 1, 3, f) == 3);
    }
    assert(fclose(f) == 0);
}

static int pontos_com(uint32_t cor)
{
    int i, n = 0;
    for (i = 0; i < ABERTURA_W * ABERTURA_H; i++) n += px[i] == cor;
    return n;
}

int main(void)
{
    int q;
    for (q = 0; q < 310; q++) {         /* a vinheta inteira não pode passar da tela */
        memset(px, 0xAB, sizeof(px));
        abertura_vinheta(px, q);
        if (q == 60) grava("vinheta_espiral");
        if (q == 200) grava("vinheta");
    }
    abertura_titulo(px, "V59");
    assert(pontos_com(0xF8D030) > 500);             /* "TEKKEN 3" em ouro */
    grava("titulo");
    abertura_apoio(px, 0, 1);
    assert(px[(32 + 2) * ABERTURA_W + 8 + 2] == 0xFFFFFF);  /* borda branca do QR */
    grava("apoio");
    for (q = 0; q <= ABERTURA_IRIS_QUADROS; q++) {      /* todos os raios, abrindo e fechando */
        int ra = abertura_iris_raio(q, 1), rf = abertura_iris_raio(q, 0);
        assert(ra == abertura_iris_raio(ABERTURA_IRIS_QUADROS - q, 0));
        abertura_apoio(px, 0, 1); abertura_iris(px, ra);
        abertura_apoio(px, 0, 1); abertura_iris(px, rf);
    }
    {   /* com o fundo em cache, a mesma imagem */
        static uint32_t cache[ABERTURA_W * ABERTURA_H], sem[ABERTURA_W * ABERTURA_H];
        abertura_titulo(sem, "V59");
        abertura_cache(cache);
        abertura_titulo(px, "V59"); assert(!memcmp(px, sem, sizeof(sem)));
        abertura_titulo(px, "V59"); assert(!memcmp(px, sem, sizeof(sem)));
        abertura_cache(NULL);
    }
    abertura_iris(px, 0);
    assert(pontos_com(0) == ABERTURA_W * ABERTURA_H);
    assert(abertura_iris_raio(0, 1) == 0 && abertura_iris_raio(ABERTURA_IRIS_QUADROS, 1) == ABERTURA_IRIS_MAX);
    printf("abertura_test: OK (telas em build/tests/abertura_*.ppm)\n");
    return 0;
}
