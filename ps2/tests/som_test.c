/*
 * som_test.c — V47: o decodificador XA e a conversão de taxa de runtime/som.c
 * contra a 1ª versão (fórmula do psx-spx escrita amostra a amostra, posição
 * em Q16 com 64 bits). Inclui som.c para ver as variáveis internas.
 *
 * Decodificação: igual bit a bit. Conversão: mesma quantidade de quadros e
 * diferença de no máximo 1 (a nova usa a posição em Q15 na multiplicação).
 * Também: setor em pedaços = setor inteiro, buffer cheio e a fila de setores.
 */
#include <assert.h>
#include <math.h>
#include "../runtime/som.c"

/* V59: CD-DA — substitutos do cdrom.c */
static int cdda_fins, cdda_fins_normais;
int ps2_cdrom_io_livre(void) { return 1; }
void ps2_cdrom_cdda_fim(int normal) { cdda_fins++; cdda_fins_normais += normal; }

/* VBlanks até o CD-DA parar (limite: 'max') */
static unsigned vblanks_ate_parar(unsigned max)
{
    unsigned v = 0;
    while (cdda_ativo && v < max) { cdda_vblank(); anel_n = 0; v++; }   /* o "IOP" leva tudo */
    return v;
}

/* Faixa 2 falsa com 160 setores (o INDEX 01 em 150): o Play no INDEX 01
 * toca 10 setores em ~8 VBlanks (75 setores/s) e avisa o fim. */
static void teste_cdda(void)
{
    const char *nome = "build/tests/Tekken 3 (USA) (Track 2).bin";
    FILE *f = fopen(nome, "wb");
    unsigned i, v;
    int ok = f != NULL;
    for (i = 0; ok && i < 160u * 588u; i++) {
        int16_t a = (int16_t)(10000.0 * sin(2.0 * 3.14159265 * 1000.0 * i / 44100.0));
        int16_t s[2] = { a, (int16_t)-a };
        ok = fwrite(s, 4, 1, f) == 1;
    }
    if (f && fclose(f) != 0) ok = 0;
    assert(ok);
    ps2_som_cdda_abrir("build/tests/");
    assert(cdda_arq);
    ganho[0] = ganho[1] = 32767;
    cd_mudo = 0;

    /* fim do arquivo antes do fim da faixa (o arquivo de teste é curto): erro, sem DataEnd */
    cdda_tocar(CD_T1 + 150u);
    assert(cdda_ativo && cdda_le && ps2_som_cdda_lba() == CD_T1 + 150u);
    v = vblanks_ate_parar(100u);
    assert(!cdda_ativo && cdda_fins == 1 && cdda_fins_normais == 0);
    assert(v == 3u);                                 /* lê à frente: 4 + 4, e o 3º lote passa do fim */
    assert(ps2_som_cdda_lba() >= CD_T1 + 150u && ps2_som_cdda_lba() < CD_T1 + 160u);

    /* faixa 3: silêncio, anda a 75 setores/s e avisa o fim normal */
    cdda_tocar(CD_T1 + CD_T2 + CD_T3 - 10u);
    assert(cdda_ativo && !cdda_le);
    v = vblanks_ate_parar(100u);
    assert(!cdda_ativo && cdda_fins == 2 && cdda_fins_normais == 1 && v >= 7u && v <= 10u);

    /* mudo: a posição anda no mesmo ritmo, sem ler */
    cdda_tocar(CD_T1 + 150u);
    cd_mudo = 1;
    {
        unsigned long lidos = cdda_setores;
        for (i = 0; i < 60u; i++) { cdda_vblank(); anel_n = 0; }
        assert(ps2_som_cdda_lba() == CD_T1 + 150u + 75u);    /* 1 s = 75 setores */
        assert(cdda_setores == lidos);                       /* sem ler nada */
    }
    cd_mudo = 0;

    /* Stop para; faixa de dados não toca e avisa o fim */
    ps2_som_cd_cmd(0x08, NULL, 0);
    assert(!cdda_ativo);
    cdda_tocar(1000u);
    assert(!cdda_ativo && cdda_fins == 3);

    fclose(cdda_arq); cdda_arq = NULL;
    remove(nome);
    printf("som_test: cdda ok (75 setores/s, fim, mudo, faixa 3, stop)\n");
}

#include <stdlib.h>

void ps2_diag(unsigned e, uint32_t a) { (void)e; (void)a; }

/* ---- 1ª versão (referência) ---- */
static int r_hist[2][2];
static int16_t r_esq[4032], r_dir[4032];
static int r_a[2];
static int32_t r_frac, r_passo;
static int16_t r_saida[2 * 12000];
static unsigned r_n;

static int r_amostra(int lado, int s, int filtro)
{
    int v = s + ((r_hist[lado][0] * K0[filtro] + r_hist[lado][1] * K1[filtro] + 32) >> 6);
    if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
    r_hist[lado][1] = r_hist[lado][0]; r_hist[lado][0] = v;
    return v;
}

static unsigned r_decodificar(const uint8_t *setor, uint8_t cod)
{
    int estereo = cod & 1u, oito = (cod & 0x10u) != 0;
    unsigned unidades = oito ? 4u : 8u, g, u, i, n = 0;
    for (g = 0; g < 18u; g++) {
        const uint8_t *grupo = setor + 24u + g * 128u;
        for (u = 0; u < unidades; u++) {
            uint8_t p = grupo[4u + u];
            int sh = p & 15, f = (p >> 4) & 3, lado = estereo ? (int)(u & 1u) : 0;
            unsigned base;
            if (sh > 12) sh = 9;
            base = estereo ? n + (u >> 1) * 28u : n + u * 28u;
            for (i = 0; i < 28u; i++) {
                int s;
                if (oito) s = (int)(int16_t)(uint16_t)(grupo[16u + i * 4u + u] << 8) >> sh;
                else {
                    uint8_t b = grupo[16u + i * 4u + (u >> 1)];
                    s = (int)(int16_t)(uint16_t)(((u & 1u) ? (b >> 4) : (b & 15u)) << 12) >> sh;
                }
                s = r_amostra(lado, s, f);
                if (!estereo) { r_esq[base + i] = r_dir[base + i] = (int16_t)s; }
                else if (lado == 0) r_esq[base + i] = (int16_t)s;
                else r_dir[base + i] = (int16_t)s;
            }
        }
        n += estereo ? (unidades / 2u) * 28u : unidades * 28u;
    }
    return n;
}

static void r_entrada(int l, int r, int g0, int g1)
{
    int b0 = l * g0 >> 15, b1 = r * g1 >> 15;
    int d0 = b0 - r_a[0], d1 = b1 - r_a[1];
    while (r_frac < 65536) {
        r_saida[2u * r_n] = (int16_t)(r_a[0] + (int)(((int64_t)d0 * r_frac) >> 16));
        r_saida[2u * r_n + 1u] = (int16_t)(r_a[1] + (int)(((int64_t)d1 * r_frac) >> 16));
        r_n++;
        r_frac += r_passo;
    }
    r_frac -= 65536;
    r_a[0] = b0; r_a[1] = b1;
}

static int falhas;
#define CONFERIR(c, ...) do { if (!(c)) { printf(__VA_ARGS__); printf("\n"); falhas++; } } while (0)

static void setor_aleatorio(uint8_t *s, int extremos)
{
    unsigned k;
    for (k = 0; k < 2352u; k++) s[k] = (uint8_t)(rand() & 0xFF);
    if (extremos)   /* filtro 3 e shift 0: força a saturação */
        for (k = 0; k < 18u; k++) memset(s + 24u + k * 128u + 4u, 0x30, 12u);
}

static void um_formato(uint8_t cod, int cdvol)
{
    static uint8_t setor[2352];
    int estereo = cod & 1u, sat = 0;
    unsigned hz = (cod & 4u) ? 18900u : 37800u, s, i;
    memset(hist, 0, sizeof(hist)); memset(r_hist, 0, sizeof(r_hist));
    rs_a[0] = rs_a[1] = r_a[0] = r_a[1] = 0; rs_frac = r_frac = 0;
    anel_ini = anel_n = 0; saida_descartes = 0;
    g_cdvol[0] = g_cdvol[1] = 0;
    if (cdvol >= 0) {
        ps2_som_spu_escrita(0xDB0, (uint32_t)cdvol, 2);
        ps2_som_spu_escrita(0xDB2, (uint32_t)(cdvol / 2), 2);
    }
    calcular_ganho();
    for (s = 0; s < 60u; s++) {
        unsigned n_ref, max_dif = 0;
        setor_aleatorio(setor, (s % 7u) == 3u);
        n_ref = r_decodificar(setor, cod);
        /* referência com o mesmo passo (o buffer começa vazio nos dois) */
        anel_ini = anel_n = 0;
        preparar_passo(hz);
        r_passo = rs_passo;
        decodificar(setor, cod, 0, 18);
        for (i = 0; i < n_ref; i++) {
            if (dec_l[i] != r_esq[i] || (estereo ? dec_r[i] : dec_l[i]) != r_dir[i]) {
                CONFERIR(0, "cod %02x setor %u amostra %u: %d/%d, esperado %d/%d", cod, s, i,
                         dec_l[i], estereo ? dec_r[i] : dec_l[i], r_esq[i], r_dir[i]);
                return;
            }
            if (r_esq[i] == 32767 || r_esq[i] == -32768) sat++;
        }
        r_n = 0;
        for (i = 0; i < n_ref; i++) r_entrada(r_esq[i], r_dir[i], ganho[0], ganho[1]);
        CONFERIR(anel_n == r_n, "cod %02x setor %u: %u quadros, esperado %u", cod, s, anel_n, r_n);
        for (i = 0; i < 2u * r_n && i < 2u * anel_n; i++) {
            int d = anel[i] - r_saida[i];
            if (d < 0) d = -d;
            if ((unsigned)d > max_dif) max_dif = (unsigned)d;
        }
        CONFERIR(max_dif <= 1u, "cod %02x setor %u: diferença %u na conversão", cod, s, max_dif);
    }
    CONFERIR(sat > 0, "cod %02x: nenhuma amostra saturada (o teste não pegou o limite)", cod);
    CONFERIR(saida_descartes == 0, "cod %02x: %lu descartes", cod, saida_descartes);
}

/* Em pedaços (como no VBlank) dá o mesmo som que o setor inteiro. */
static int16_t ped_a[2u * 8u * 10500u], ped_b[2u * 8u * 10500u];
static unsigned ped_sequencia(uint8_t setores[][2352], uint8_t cod, const unsigned *cortes, int16_t *saida)
{
    unsigned s, k, n = 0;
    memset(hist, 0, sizeof(hist));
    rs_a[0] = rs_a[1] = 0; rs_frac = 0;
    for (s = 0; s < 8u; s++) {
        anel_ini = anel_n = 0;
        for (k = 0; k < 3u; k++)
            if (cortes[k + 1u] > cortes[k]) decodificar(setores[s], cod, cortes[k], cortes[k + 1u]);
        memcpy(saida + 2u * n, anel, anel_n * 4u);
        n += anel_n;
    }
    return n;
}
static void pedacos(uint8_t cod)
{
    static uint8_t setores[8][2352];
    static const unsigned inteiro[4] = { 0, 18, 18, 18 };
    static const unsigned cortes[][4] = { { 0, 6, 12, 18 }, { 0, 1, 17, 18 }, { 0, 9, 9, 18 } };
    unsigned s, c, na, nb;
    for (s = 0; s < 8u; s++) setor_aleatorio(setores[s], s == 3u);
    g_cdvol[0] = g_cdvol[1] = 0;
    calcular_ganho();
    na = ped_sequencia(setores, cod, inteiro, ped_a);
    for (c = 0; c < 3u; c++) {
        nb = ped_sequencia(setores, cod, cortes[c], ped_b);
        CONFERIR(nb == na && !memcmp(ped_a, ped_b, na * 4u),
                 "cod %02x cortes %u: %u quadros (inteiro %u) ou som diferente", cod, c, nb, na);
    }
}

int main(void)
{
    static const uint8_t cods[] = { 0x01, 0x00, 0x11, 0x10, 0x05, 0x04 };
    unsigned k;
    srand(47);
    for (k = 0; k < sizeof(cods); k++) {
        um_formato(cods[k], -1);
        um_formato(cods[k], 0x2345);
        pedacos(cods[k]);
    }
    /* buffer cheio: descarta o som novo e não mexe no começo */
    {
        static uint8_t setor[2352];
        unsigned ini;
        setor_aleatorio(setor, 0);
        g_cdvol[0] = g_cdvol[1] = 0;
        anel_ini = 100u; anel_n = ANEL - 10u; ini = anel_ini; saida_descartes = 0;
        decodificar(setor, 0x01, 0, 18);
        CONFERIR(anel_n == ANEL && anel_ini == ini && saida_descartes > 0,
                 "buffer cheio: n %u ini %u descartes %lu", anel_n, anel_ini, saida_descartes);
    }
    /* fila: nada é decodificado na entrega do setor; decodificar_fila anda
     * por grupos; fila cheia termina o 1º setor; troca de canal descarta */
    {
        static uint8_t setor[2352];
        unsigned k2;
        uint8_t p[2] = { 1, 3 };
        setor_aleatorio(setor, 0);
        setor[16] = 1; setor[17] = 3; setor[19] = 0x01;
        modo_cd = 0x48u;                    /* XA ligado, com filtro */
        ps2_som_cd_cmd(0x0D, p, 0);
        anel_ini = anel_n = 0; fila_ini = fila_n = fila_grupo = 0;
        dec_cheia = dec_ocioso = 0; xa_tocados = 0; saida_descartes = 0;
        for (k2 = 0; k2 < FILA_SETORES; k2++) ps2_som_xa(setor, 1);
        CONFERIR(fila_n == FILA_SETORES && anel_n == 0, "entrega: %u na fila, %u quadros", fila_n, anel_n);
        decodificar_fila(GRUPOS_VB - 1u);
        CONFERIR(ps2_som_ocioso() == 1 && dec_ocioso == 1, "espera: não decodificou");
        CONFERIR(fila_n == FILA_SETORES && fila_grupo == GRUPOS_VB && anel_n > 0,
                 "pedaço: %u na fila, grupo %u, %u quadros", fila_n, fila_grupo, anel_n);
        ps2_som_xa(setor, 1);               /* cheia: termina o 1º e entra */
        CONFERIR(fila_n == FILA_SETORES && fila_grupo == 0 && dec_cheia == 18u - GRUPOS_VB && xa_tocados == 1,
                 "fila cheia: %u na fila, grupo %u, %lu tocados", fila_n, fila_grupo, xa_tocados);
        setor[17] = 4;                      /* outro canal: fora do filtro */
        ps2_som_xa(setor, 1);
        CONFERIR(fila_n == FILA_SETORES, "fora do filtro entrou na fila");
        decodificar_fila(2u * 18u + 5u);
        CONFERIR(fila_n == 2u && fila_grupo == 5u && xa_tocados == 3,
                 "41 grupos: %u na fila, grupo %u, %lu tocados", fila_n, fila_grupo, xa_tocados);
        decodificar_fila(100u);
        CONFERIR(fila_n == 0 && fila_grupo == 0 && xa_tocados == 5 && saida_descartes == 0,
                 "resto: %u na fila, grupo %u, %lu tocados", fila_n, fila_grupo, xa_tocados);
        CONFERIR(ps2_som_ocioso() == 0, "espera com a fila vazia devolveu 1");
        setor[17] = 3;
        ps2_som_xa(setor, 1); ps2_som_xa(setor, 1);
        decodificar_fila(3u);
        p[1] = 4; ps2_som_cd_cmd(0x0D, p, 0);   /* troca de canal: fila descartada */
        CONFERIR(fila_n == 0 && fila_grupo == 0, "troca de canal: %u na fila, grupo %u", fila_n, fila_grupo);
    }
    /* som antigo no buffer: sai na hora, menos o envio em voo (voo_q), que
     * o VBlank seguinte resolve (tirar_do_buffer: o que o IOP aceitou e o
     * resto); o som novo decodificado depois fica */
    {
        static uint8_t setor[2352];
        unsigned antigo, novo, ini;
        uint8_t p[2] = { 1, 5 };
        setor_aleatorio(setor, 0);
        g_cdvol[0] = g_cdvol[1] = 0; cd_mudo = 0;
        anel_ini = 300u; anel_n = 0; descartar_q = 0; saida_cortados = 0; saida_enviados = 0;
        decodificar(setor, 0x01, 0, 18);
        decodificar(setor, 0x01, 0, 18);
        antigo = anel_n;
        voo_q = 1500u;                       /* 1500 do começo a caminho do IOP */
        ps2_som_cd_cmd(0x0D, p, 0);          /* outro canal */
        CONFERIR(anel_n == 1500u && descartar_q == 1500u && saida_cortados == antigo - 1500u &&
                 rs_frac == 0 && houve_corte, "corte: ficaram %u, marcou %u de %u", anel_n, descartar_q, antigo);
        decodificar(setor, 0x01, 0, 18);     /* música nova antes do VBlank */
        novo = anel_n - 1500u;
        CONFERIR(tirar_do_buffer(1000u) == 1, "corte: tirar_do_buffer não avisou");   /* IOP aceitou 1000 */
        CONFERIR(anel_n == novo && saida_cortados == antigo - 1000u && saida_enviados == 1000u &&
                 anel_ini == ((300u + 1500u) & (ANEL - 1u)) && descartar_q == 0 && voo_q == 0,
                 "corte: ficaram %u (novo %u), cortados %lu, ini %u", anel_n, novo, saida_cortados, anel_ini);
        CONFERIR(tirar_do_buffer(0u) == 0, "tirar_do_buffer avisou corte de novo");
        /* Mute sem envio em voo: corta tudo na hora; o que vem depois sai mudo */
        ini = anel_ini;
        ps2_som_cd_cmd(0x0B, p, 0);
        CONFERIR(anel_n == 0 && descartar_q == 0 && cd_mudo == 1, "mute: ficaram %u", anel_n);
        decodificar(setor, 0x01, 0, 18);
        CONFERIR(tirar_do_buffer(0u) == 1, "mute: tirar_do_buffer não avisou");
        {
            unsigned i, nz = 0;
            for (i = 0; i < anel_n; i++) {
                unsigned k = (anel_ini + i) & (ANEL - 1u);
                if (anel[2u * k] || anel[2u * k + 1u]) nz++;
            }
            CONFERIR(anel_n > 2000u && nz == 0 && anel_ini == ini, "mute: %u quadros, %u com som", anel_n, nz);
        }
        ps2_som_cd_cmd(0x0C, p, 0);          /* Demute: o silêncio do mudo sai */
        CONFERIR(anel_n == 0 && cd_mudo == 0, "demute: ficaram %u", anel_n);
        /* Init: corta e limpa a fila */
        setor[16] = 1; setor[17] = 5; modo_cd = 0x48u;
        ps2_som_xa(setor, 1);
        decodificar(setor, 0x01, 0, 18);
        ps2_som_cd_cmd(0x0A, p, 0);
        CONFERIR(fila_n == 0 && anel_n == 0 && modo_cd == 0 && hist[0][0] == 0,
                 "init: fila %u, buffer %u", fila_n, anel_n);
    }
    if (falhas) { printf("som_test: %d falhas\n", falhas); return 1; }
    teste_cdda();
    printf("som_test: ok\n");
    return 0;
}
