/*
 * codigo.c — guarda o executável original e decide se o código compilado
 * de um trecho ainda vale (ver ram.h: ps2_cod_marcar).
 */
#include "ram.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


uint32_t g_cod_ini, g_cod_tam;          /* g_cod_tam = 0 até registrar */
uint8_t  g_cod_sujo[PS2_COD_SUJO_BYTES];
uint32_t g_cod_ver = 1;                /* V31: ver ram.h */
/* g_cod_pag (V56): definido em ram.c, dentro do bloco quente (ver ram.h) */

static void paginas(uint32_t ini, uint32_t tam, uint8_t v)
{
    uint32_t p;
    if (!tam) return;
    for (p = ini >> PS2_COD_PAG_BITS; p <= (ini + tam - 1u) >> PS2_COD_PAG_BITS && p < sizeof(g_cod_pag); p++)
        g_cod_pag[p] = v;
}

/* Marcação de verdade (antes em linha em ram.h): grânulo de 64 bytes da
 * imagem do executável; grânulo já sujo não grava de novo (V31). */
void ps2_cod_marcar_lento(uint32_t addr)
{
    uint32_t o = (addr & PS2_RAM_MASK) - g_cod_ini;
    if (o < g_cod_tam) {
        uint8_t *b = &g_cod_sujo[o >> 9], m = (uint8_t)(1u << ((o >> 6) & 7u));
        if (!(*b & m)) { *b |= m; g_cod_ver++; }
    }
}

void ps2_cod_so_codigo(const uint32_t *faixas, uint32_t n)
{
    uint32_t i;
    memset(g_cod_pag, 0, sizeof(g_cod_pag));
    for (i = 0; i < n; i++) {
        uint32_t ini = faixas[2u * i] & PS2_RAM_MASK, tam = faixas[2u * i + 1u];
        /* só o que cai na imagem do executável (o resto não tem grânulo) */
        if (ini < g_cod_ini) { uint32_t d = g_cod_ini - ini; if (d >= tam) continue; ini += d; tam -= d; }
        if (ini >= g_cod_ini + g_cod_tam) continue;
        if (tam > g_cod_ini + g_cod_tam - ini) tam = g_cod_ini + g_cod_tam - ini;
        paginas(ini, tam, 1);
    }
}
static uint8_t *cod_orig;

/* Chamado pelo loader depois de copiar o EXE para a RAM. */
void ps2_cod_registrar(uint32_t endereco, uint32_t tamanho)
{
    cod_orig = (uint8_t *)malloc(tamanho);
    if (!cod_orig) {
        /* Sem a cópia não dá para saber se um overlay sobrescreveu código:
         * rodar código compilado antigo seria pior que parar. */
        printf("codigo: ERRO sem memoria para a copia do executavel (%lu bytes)\n",
               (unsigned long)tamanho);
        fflush(stdout);
        abort();
    }
    memcpy(cod_orig, ps2_ram_data() + (endereco & PS2_RAM_MASK), tamanho);
    memset(g_cod_sujo, 0, sizeof(g_cod_sujo));
    g_cod_ini = endereco & PS2_RAM_MASK;
    g_cod_tam = tamanho;
    memset(g_cod_pag, 0, sizeof(g_cod_pag));
    paginas(g_cod_ini, g_cod_tam, 1);   /* até ps2_cod_so_codigo: o executável todo */
    g_cod_ver++;
}

/* 1 se [ini, ini+len) ainda é igual ao executável original. */
static int trecho_original(uint32_t ini, uint32_t len)
{
    uint32_t o = (ini & PS2_RAM_MASK) - g_cod_ini, fim, g;
    if (!cod_orig || o >= g_cod_tam) return 1;
    fim = o + len;
    if (fim > g_cod_tam) fim = g_cod_tam;
    for (g = o >> 6; g <= (fim - 1u) >> 6; g++) {
        if (!(g & 7u) && g + 7u <= (fim - 1u) >> 6 && !g_cod_sujo[g >> 3]) {
            g += 7u;                    /* 8 grânulos limpos de uma vez */
            continue;
        }
        if (!(g_cod_sujo[g >> 3] & (1u << (g & 7u))))
            continue;
        /* grânulo sujo: compara com o original (V48: o último pode ser parcial) */
        {
            uint32_t a = g << 6, n = g_cod_tam - a < 64u ? g_cod_tam - a : 64u;
            if (memcmp(ps2_ram_data() + g_cod_ini + a, cod_orig + a, n) != 0)
                return 0;
            g_cod_sujo[g >> 3] &= (uint8_t)~(1u << (g & 7u));   /* igual: limpa */
        }
    }
    return 1;
}

/* Chamado pelo despacho gerado: ranges = pares {inicio, tamanho}. */
int dirty_ram_text_native_ok_ranges_from(const uint32_t *ranges, uint32_t count,
                                          uint32_t pc)
{
    uint32_t i;
    (void)pc;
    for (i = 0; i < count; i++)
        if (!trecho_original(ranges[2u * i], ranges[2u * i + 1u]))
            return 0;
    return 1;
}
