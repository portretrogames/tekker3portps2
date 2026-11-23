/*
 * som.c — som do port.
 *
 * V47: música XA do CD. O Tekken 3 toca a música em XA-ADPCM (arquivo 1, 8
 * canais intercalados, estéreo 37,8 kHz 4 bits, CD em 2x; o jogo escolhe o
 * canal pelo Setfilter). No PS1 o controlador do CD decodifica e manda para
 * a entrada de CD da SPU. Aqui: cdrom.c entrega cada setor de áudio lido,
 * que entra numa fila; ele é decodificado (fórmula do psx-spx) aos poucos no
 * tempo em que o jogo espera o quadro (bios_hle.c), convertido para 48 kHz
 * por interpolação linear e enviado ao audsrv (kit/som.h) uma vez por
 * VBlank, sem esperar a resposta do IOP.
 * Volume: entrada de CD da SPU (1F801DB0/DB2), volume principal (1F801D80/
 * D82, modo fixo), SPUCNT (SPU ligada, sem mudo, entrada de CD ligada) e
 * Mute/Demute do CD. Registrador que o jogo nunca gravou vale o padrão
 * (volume cheio, ligado), para não ficar mudo por um valor que não vimos.
 *
 * Efeitos e vozes: a SPU do PS1 toca na SPU2 (spu.c). CD-DA da faixa 2: V59.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#ifdef _EE
#include "kit/som.h"
#endif
#include "cdrom.h"
#include "som.h"
#include "spu.h"

/* estado do CD que mexe no som: modo, filtro XA e mudo */
static uint8_t filtro_arq, filtro_can, modo_cd, cd_mudo;

/* registradores da SPU que mexem no som do CD (valor e se o jogo já gravou) */
static uint16_t r_cdvol[2], r_mainvol[2], r_spucnt;
static uint8_t g_cdvol[2], g_mainvol[2], g_spucnt;

static void gravar_reg16(uint32_t off, uint16_t v)
{
    if (off == 0xDB0) { r_cdvol[0] = v; g_cdvol[0] = 1; }
    else if (off == 0xDB2) { r_cdvol[1] = v; g_cdvol[1] = 1; }
    else if (off == 0xD80) { r_mainvol[0] = v; g_mainvol[0] = 1; }
    else if (off == 0xD82) { r_mainvol[1] = v; g_mainvol[1] = 1; }
    else if (off == 0xDAA) { r_spucnt = v; g_spucnt = 1; }
}

/* Escrita em 0x1F801C00-0x1F801FFF (off = endereço - 0x1F801000). */
void ps2_som_spu_escrita(uint32_t off, uint32_t v, unsigned w)
{
    ps2_spu_escrita(off, v, w);         /* V53: vai para a SPU2 */
    gravar_reg16(off, (uint16_t)v);
    if (w == 4) gravar_reg16(off + 2u, (uint16_t)(v >> 16));
}

/* DMA canal 4 (RAM -> SPU) pedido: bcr = tamanho do bloco e quantidade. */
void ps2_som_dma4(uint32_t madr, uint32_t bcr, uint32_t chcr)
{
    ps2_spu_dma4(madr, bcr, chcr);            /* V53: memória da SPU (e SPU2) */
}

/* ------------------------------------------------------------------ */
/* saída: 48 kHz estéreo, buffer circular no EE                         */
#define SAIDA_HZ 48000u
#define ANEL 16384u                         /* quadros: ~341 ms */
#define PARTIDA 7680u                       /* começa a tocar com ~160 ms guardados */
/* Sem música: silêncio sem esperar o IOP, o máximo de um envio (o IOP gasta
 * 800,8 quadros por VBlank e recusa o que não couber), para a fila dele nunca
 * secar, nem depois de VBlanks perdidos (o audsrv toca de novo o que sobrou
 * na fila se ninguém mandar nada). Custo: a fila do IOP fica cheia de
 * silêncio, até ~107 ms a mais antes da música começar. Antes eram 2
 * perguntas por VBlank esperando a resposta (~210 us no PCSX2). */
#define SILENCIO_VB 1024u
/* O começo do buffer pode estar a caminho do IOP (envio sem esperar): só
 * ps2_som_vblank() anda com anel_ini. Buffer cheio descarta o som NOVO. */
static int16_t anel[ANEL * 2u];
static unsigned anel_ini, anel_n;
#ifdef _EE
static int tocando;
#endif
/* Troca de canal, Mute/Demute e Init do CD: o som antigo do buffer sai na
 * hora, menos os voo_q quadros do começo que estão a caminho do IOP (o
 * VBlank seguinte tira o que o IOP aceitou e joga o resto fora: descartar_q).
 * O que já está na fila do IOP (até ~107 ms) ainda toca. */
static unsigned voo_q, descartar_q;
static int houve_corte;                     /* cortar_saida() desde o último tirar_do_buffer() */

/* O IOP aceitou 'aceitos' quadros do começo do buffer; depois sai o resto do
 * som antigo marcado em descartar_q. Devolve 1 se houve corte desde a última
 * vez. */
static inline int tirar_do_buffer(unsigned aceitos)
{
    int cortou = houve_corte;
    houve_corte = 0;
    if (aceitos > anel_n) aceitos = anel_n;
    anel_ini = (anel_ini + aceitos) & (ANEL - 1u);
    anel_n -= aceitos;
    if (descartar_q) {
        unsigned d = descartar_q > aceitos ? descartar_q - aceitos : 0u;
        if (d > anel_n) d = anel_n;
        anel_ini = (anel_ini + d) & (ANEL - 1u);
        anel_n -= d;
        descartar_q = 0;
    }
    voo_q = 0;
    return cortou;
}

/* ganho Q15 de cada lado (0 a 32767), recalculado a cada setor */
static int ganho[2];
static int s16(uint16_t v) { return (int)(int16_t)v; }
static void calcular_ganho(void)
{
    int c;
    int ligado = !g_spucnt ||
                 ((r_spucnt & 0x8000u) && (r_spucnt & 0x4000u) && (r_spucnt & 0x0001u));
    for (c = 0; c < 2; c++) {
        long g = 32767;
        if (g_cdvol[c]) { int v = s16(r_cdvol[c]); g = v < 0 ? -v : v; }
        if (g_mainvol[c] && !(r_mainvol[c] & 0x8000u)) {   /* modo fixo: bits 0-14 = volume/2 */
            int v = (int)((int16_t)(r_mainvol[c] << 1));
            g = g * (v < 0 ? -v : v) / 32767;
        }
        if (!ligado || cd_mudo) g = 0;
        ganho[c] = (int)(g > 32767 ? 32767 : g);
    }
}

/* conversão de taxa: interpolação linear entre o quadro anterior e o atual.
 * O CD emulado e a saída do SPU2 (48 kHz do cristal do PS2) não andam
 * exatamente juntos: no PCSX2 chegavam ~1% menos setores e o buffer secava a
 * cada ~4 s (falta = ~110 ms de silêncio). A taxa de entrada é ajustada pelo
 * nível do buffer em até ±1% (17 centésimos de semitom, não se ouve), para
 * ele ficar perto de NIVEL (medido na hora de decodificar). Com o CD 1% mais
 * lento o nível para em NIVEL - 8 x 1% de 37800 = ~3100 quadros (~65 ms). */
#define NIVEL 6144u
static int rs_a[2];
static int32_t rs_frac;                       /* posição entre a e b em Q16 (0..65535) */
static int32_t rs_passo;                      /* hz de entrada / 48000 em Q16 */
/* Passo da conversão para o próximo setor, com a correção pelo nível do
 * buffer. Q16 e multiplicação (no EE a divisão de 64 bits por amostra, da
 * 1ª versão, custava ~0,3 ms por quadro no PCSX2). */
static void preparar_passo(unsigned hz)
{
    int corr = ((int)NIVEL - (int)anel_n) / 8, lim = (int)hz / 100;
    if (corr > lim) corr = lim; else if (corr < -lim) corr = -lim;
    rs_passo = (int32_t)((((uint32_t)((int)hz - corr)) << 16) / SAIDA_HZ);   /* buffer baixo: passo menor = mais quadros */
}
/* Converte n quadros (l[i], r[i]) para 48 kHz e põe no fim do buffer.
 * V47: tudo em variáveis locais num laço só (a 1ª versão chamava uma função
 * por quadro de saída e custava ~1,4 ms por setor no PCSX2). Posição em Q15
 * na multiplicação: d (até ±65535) x f (até 32767) cabe em 32 bits. */
static void reamostrar(const int16_t *l, const int16_t *r, unsigned n)
{
    int g0 = ganho[0], g1 = ganho[1], a0 = rs_a[0], a1 = rs_a[1];
    int32_t frac = rs_frac, passo = rs_passo;
    unsigned livre = ANEL - anel_n, cab = (anel_ini + anel_n) & (ANEL - 1u), saida = 0, i;
    for (i = 0; i < n; i++) {
        int b0 = (l[i] * g0) >> 15, b1 = (r[i] * g1) >> 15;
        int d0 = b0 - a0, d1 = b1 - a1;
        while (frac < 65536) {
            int f = frac >> 1;
            if (saida < livre) {
                int16_t *o = &anel[2u * cab];
                o[0] = (int16_t)(a0 + ((d0 * f) >> 15));
                o[1] = (int16_t)(a1 + ((d1 * f) >> 15));
                cab = (cab + 1u) & (ANEL - 1u);
                saida++;
            }
            frac += passo;
        }
        frac -= 65536;
        a0 = b0; a1 = b1;
    }
    anel_n += saida;
    rs_frac = frac; rs_a[0] = a0; rs_a[1] = a1;
}

/* ------------------------------------------------------------------ */
/* decodificador XA-ADPCM (psx-spx: "CDROM XA Audio ADPCM Compression") */
static const int K0[4] = { 0, 60, 115, 98 }, K1[4] = { 0, 0, -52, -55 };
static int hist[2][2];                      /* [lado][0 = anterior, 1 = antes dele] */

/* Decodifica os grupos g0..g1-1 de um setor (2352 bytes; dados de áudio a
 * partir do byte 24: 18 grupos de 128 bytes) e manda para a saída. cod =
 * byte de codificação. Decodificar 0-6, 6-12 e 12-18 dá o mesmo som que o
 * setor inteiro (o passo da conversão é escolhido no grupo 0).
 * Cada unidade (28 amostras) tem o seu laço com filtro e histórico em
 * variáveis locais. A amostra de 4 bits vai para os bits 28-31 (nibble
 * baixo: << 28; alto: << 24 e máscara) e volta com deslocamento aritmético
 * de 16 + shift: o mesmo que (int16)(nibble << 12) >> shift. */
static int16_t dec_l[4032], dec_r[2016];   /* mono usa só dec_l (até 4032) */
static void decodificar(const uint8_t *setor, uint8_t cod, unsigned g0, unsigned g1)
{
    int estereo = cod & 1u, oito = (cod & 0x10u) != 0;
    unsigned hz = (cod & 4u) ? 18900u : 37800u;
    unsigned unidades = oito ? 4u : 8u, g, u, n = 0;
    for (g = g0; g < g1; g++) {
        const uint8_t *grupo = setor + 24u + g * 128u;
        for (u = 0; u < unidades; u++) {
            uint8_t p = grupo[4u + u];
            int sh = p & 15, f = (p >> 4) & 3, lado = estereo ? (int)(u & 1u) : 0;
            int k0 = K0[f], k1 = K1[f], h0 = hist[lado][0], h1 = hist[lado][1], d;
            /* estéreo: unidades pares = esquerda, ímpares = direita, aos pares no tempo */
            int16_t *o = (lado ? dec_r : dec_l) + (estereo ? n + (u >> 1) * 28u : n + u * 28u);
            const uint8_t *b;
            unsigned i, desl;
            uint32_t masc;
            if (sh > 12) sh = 9;
            d = 16 + sh;
            if (oito) { b = grupo + 16u + u; desl = 24u; masc = 0xFF000000u; }
            else { b = grupo + 16u + (u >> 1); desl = (u & 1u) ? 24u : 28u; masc = 0xF0000000u; }
            for (i = 0; i < 28u; i++) {
                int v = ((int)(((uint32_t)b[4u * i] << desl) & masc) >> d) +
                        ((h0 * k0 + h1 * k1 + 32) >> 6);
                if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
                h1 = h0; h0 = v;
                o[i] = (int16_t)v;
            }
            hist[lado][0] = h0; hist[lado][1] = h1;
        }
        n += estereo ? (unidades / 2u) * 28u : unidades * 28u;
    }
    calcular_ganho();
    if (g0 == 0) preparar_passo(hz);
    reamostrar(dec_l, estereo ? dec_r : dec_l, n);
}

/* ------------------------------------------------------------------ */
/* CD                                                                   */

/* Fila de setores a decodificar. Decodificar um setor custa ~0,8 ms no
 * PCSX2 e o CD entrega ~19 por segundo em qualquer ponto do quadro (até no
 * meio da lógica da luta): na hora da entrega isso empurrava quadros para o
 * VBlank seguinte. Também não serve no VBlank: ele atrasa a IRQ do VBlank e
 * a do timer 2 que vem logo depois e libera o passo do jogo (testado: pior
 * nos dois casos). Agora o setor espera na fila e é decodificado um grupo
 * (1/18) por vez enquanto o jogo está parado no laço da espera do quadro
 * (bios_hle.c, ps2_som_ocioso). Se isso não der conta (tela sem esse laço,
 * quadros sem folga): reserva de GRUPOS_VB por VBlank (6 x 60 = 360/s; o
 * CD manda 18,75 x 18 = 337,5), ver ps2_som_vblank(). */
#define FILA_SETORES 4u
#define GRUPOS_VB 6u
static uint8_t fila_set[FILA_SETORES][2352];
static unsigned fila_ini, fila_n, fila_grupo;   /* fila_grupo: próximo grupo do 1º setor */

/* Decodifica até 'grupos' grupos do começo da fila. */
static void decodificar_fila(unsigned grupos)
{
    if (!fila_n) return;
    while (grupos && fila_n) {
        const uint8_t *s = fila_set[fila_ini];
        unsigned g1 = fila_grupo + grupos;
        if (g1 > 18u) g1 = 18u;
        decodificar(s, s[19], fila_grupo, g1);
        grupos -= g1 - fila_grupo;
        fila_grupo = g1;
        if (fila_grupo == 18u) {
            fila_grupo = 0;
            fila_ini = (fila_ini + 1u) % FILA_SETORES;
            fila_n--;
        }
    }
}

/* O som que já foi decodificado não toca mais (no PS1 a troca de canal e o
 * Mute/Demute valem na hora): sai do buffer agora, menos o envio em voo, que
 * o VBlank seguinte resolve (tirar_do_buffer); a conversão de taxa recomeça
 * do zero. 'fila' = também a fila de setores e o histórico do filtro (música
 * nova). */
static void cortar_saida(int fila)
{
    if (anel_n > voo_q) anel_n = voo_q;
    descartar_q = anel_n;
    houve_corte = 1;
    rs_frac = 0; rs_a[0] = rs_a[1] = 0;
    if (fila) { fila_n = 0; fila_grupo = 0; memset(hist, 0, sizeof(hist)); }
}

/* Comando do CD (cdrom.c): Setmode, Setfilter, Play, Mute/Demute, Init. */
/* ---- V59: CD-DA (só a faixa 2 tem som; a 3 é silêncio) ----
 * A faixa 2 abre na partida (ps2_som_cdda_abrir). No Play, a posição do
 * "disco" (GetlocP, fim da faixa) anda a 75 setores/s (1,25 por VBlank,
 * também com o CD mudo). A leitura vai à frente dela enquanto o buffer de
 * saída está abaixo do nível (folga contra atrasos do disco), em lotes de
 * até CDDA_LOTE setores no VBlank, só com o CD de dados livre (o fileXio
 * faz um pedido por vez); se ficar para trás (mudo), pula para a posição.
 * 44,1 -> 48 kHz
 * pelo mesmo reamostrar() da música XA (e o mesmo ganho do volume do CD).
 * Faixa 3 (zeros) ou faixa 2 sem arquivo: o tempo passa em silêncio e o fim
 * é avisado igual. */
#define CD_T1 PS2_CD_T1_SETORES
#define CD_T2 PS2_CD_T2_SETORES
#define CD_T3 PS2_CD_T3_SETORES
#define CDDA_LOTE 4u
#define CDDA_CREDITO_VB 5u              /* quartos de setor por VBlank (75/60 = 1,25) */
static FILE *cdda_arq;
static int cdda_ativo, cdda_le;         /* le: há som a ler (faixa 2 com arquivo) */
static uint32_t cdda_lba_atual, cdda_lba_fim, cdda_arq_pos = 0xFFFFFFFFu;
static uint32_t cdda_ler;               /* próximo setor (LBA) a ler */
static int cdda_drenar;                 /* fim normal: toca o que sobrou no buffer */
#ifdef _EE
static int cdda_saida_ok(void) { return kit_som_pronto(); }
#else
static int cdda_saida_ok(void) { return 1; }
#endif
static unsigned cdda_credito;           /* em quartos de setor */
static uint8_t cdda_buf[CDDA_LOTE * 2352u] __attribute__((aligned(64)));
static int16_t cdda_l[CDDA_LOTE * 588u], cdda_r[CDDA_LOTE * 588u];

void ps2_som_cdda_abrir(const char *pasta)
{
    static const char *nomes[] = { "Tekken 3 (USA) (Track 2).bin", "../Tekken 3 (USA) (Track 2).bin", "TEKKEN3_2.BIN" };
    char caminho[320];
    unsigned i;
    if (cdda_arq || !pasta) return;
    for (i = 0; i < sizeof(nomes) / sizeof(nomes[0]) && !cdda_arq; i++) {
        snprintf(caminho, sizeof(caminho), "%s%s", pasta, nomes[i]);
        cdda_arq = fopen(caminho, "rb");
    }
}

uint32_t ps2_som_cdda_lba(void) { return cdda_lba_atual; }

static void cdda_parar(void)
{
    if (cdda_ativo || cdda_drenar) cortar_saida(1);   /* interrompido: o resto não sai */
    cdda_ativo = 0; cdda_drenar = 0;
}

static void cdda_tocar(uint32_t lba)
{
    cdda_parar();
    if (lba >= CD_T1 && lba < CD_T1 + CD_T2) {
        cdda_lba_fim = CD_T1 + CD_T2;
        cdda_le = cdda_arq != NULL;
    } else if (lba >= CD_T1 + CD_T2 && lba < CD_T1 + CD_T2 + CD_T3) {
        cdda_lba_fim = CD_T1 + CD_T2 + CD_T3;
        cdda_le = 0;                    /* faixa 3: só silêncio */
    } else {                            /* faixa de dados: nada a tocar */
        ps2_cdrom_cdda_fim(1);
        return;
    }
    cdda_lba_atual = cdda_ler = lba;
    cdda_credito = 0;
    cdda_ativo = 1;
}

/* No VBlank (ps2_som_vblank). */
static __attribute__((unused)) void cdda_vblank(void)
{
    extern int ps2_cdrom_io_livre(void);
    unsigned n, i, k;
    if (!cdda_ativo) return;
    /* leitura: à frente da posição, até o buffer chegar ao nível */
    if (cdda_ler < cdda_lba_atual) cdda_ler = cdda_lba_atual;
    n = cdda_lba_fim - cdda_ler;
    if (n > CDDA_LOTE) n = CDDA_LOTE;
    if (cdda_le && !cd_mudo && n && anel_n < NIVEL && cdda_saida_ok() && ps2_cdrom_io_livre()) {
        uint32_t pos = cdda_ler - CD_T1;            /* o arquivo começa no INDEX 00 */
        if (cdda_arq_pos != pos && fseek(cdda_arq, (long)pos * 2352L, SEEK_SET) != 0) k = 0;
        else k = (unsigned)fread(cdda_buf, 2352u, n, cdda_arq);
        cdda_arq_pos = k ? pos + k : 0xFFFFFFFFu;
        if (k < n) {                    /* o arquivo acabou antes da faixa ou deu erro */
            cdda_ativo = 0;
            ps2_cdrom_cdda_fim(0);
            return;
        }
        for (i = 0; i < n * 588u; i++) {         /* 16 bits estéreo intercalado */
            cdda_l[i] = (int16_t)(cdda_buf[4u * i] | cdda_buf[4u * i + 1u] << 8);
            cdda_r[i] = (int16_t)(cdda_buf[4u * i + 2u] | cdda_buf[4u * i + 3u] << 8);
        }
        preparar_passo(44100u);
        reamostrar(cdda_l, cdda_r, n * 588u);
        cdda_ler += n;
    }
    /* posição do "disco": 1,25 setor por VBlank */
    cdda_credito += CDDA_CREDITO_VB;
    n = cdda_credito / 4u;
    if (n > cdda_lba_fim - cdda_lba_atual) n = cdda_lba_fim - cdda_lba_atual;
    cdda_credito -= 4u * n;
    cdda_lba_atual += n;
    if (cdda_lba_atual >= cdda_lba_fim) {
        cdda_ativo = 0;
        cdda_drenar = 1;                /* o fim curto sai mesmo abaixo da PARTIDA */
        ps2_cdrom_cdda_fim(1);
    }
}

void ps2_som_cd_cmd(uint8_t cmd, const uint8_t *param, uint32_t lba)
{
    if (cmd == 0x0E) modo_cd = param[0];
    else if (cmd == 0x0D) {
        if (param[0] != filtro_arq || param[1] != filtro_can) cortar_saida(1);
        filtro_arq = param[0]; filtro_can = param[1];
    }
    else if (cmd == 0x03) cdda_tocar(lba);
    /* Stop, Pause, ReadN/S, SeekL/P: o drive deixa de tocar (V59) */
    else if (cmd == 0x08 || cmd == 0x09 || cmd == 0x06 || cmd == 0x1B || cmd == 0x15 || cmd == 0x16) cdda_parar();
    else if (cmd == 0x0B) { if (!cd_mudo) cortar_saida(0); cd_mudo = 1; }
    else if (cmd == 0x0C) { if (cd_mudo) cortar_saida(0); cd_mudo = 0; }   /* o silêncio do mudo sai */
    else if (cmd == 0x0A || cmd == 0x1C) {        /* Init/Reset: modo 0 (cdrom.c) */
        cd_mudo = 0; modo_cd = 0; cdda_ativo = 0;   /* cortar_saida logo abaixo */
        cortar_saida(1);
    }
}

/* Setor lido do disco com o bit de áudio no submode (setor 2352: o
 * subcabeçalho começa no byte 16). xa_ligado = Setmode com o bit 6. */
void ps2_som_xa(const uint8_t *setor, int xa_ligado)
{
    uint8_t arq = setor[16], can = setor[17];
    int ok;
    if (!xa_ligado) return;
    /* V59: tocando CD-DA o drive do PS1 não lê setores (nem XA) */
    if (cdda_ativo) return;
    ok = !(modo_cd & 0x08u) || (arq == filtro_arq && can == filtro_can);
#ifdef _EE
    if (!kit_som_pronto()) ok = 0;          /* sem saída de som: nem decodifica */
#endif
    if (ok) {
        if (fila_n == FILA_SETORES) decodificar_fila(18u - fila_grupo);
        memcpy(fila_set[(fila_ini + fila_n) % FILA_SETORES], setor, 2352u);
        fila_n++;
    }
}

int ps2_som_pendente(void) { return fila_n != 0; }

/* O jogo está parado no laço da espera do quadro (bios_hle.c): decodifica
 * um grupo. Devolve 1 se havia o que fazer. */
int ps2_som_ocioso(void)
{
    if (!fila_n) return 0;
    decodificar_fila(1u);
    return 1;
}

/* ------------------------------------------------------------------ */
/* uma vez por VBlank do PS1 (bios_hle.c)                               */
void ps2_som_vblank(void)
{
#ifdef _EE
    static int enviou_silencio;              /* o envio em voo é de silêncio */
    int aceitos;
    unsigned n, a;
    cdda_vblank();                           /* V59: o relógio do CD-DA anda mesmo sem saída de som */
    if (!kit_som_pronto()) return;
    ps2_spu_vblank();                        /* V53: pacote da SPU do PS1 */
    /* Reserva: a espera do quadro não está dando conta (tela sem o laço, ou
     * quadros sem folga): mais de 1 setor parado na fila, ou o buffer do EE
     * abaixo de 1/4 do nível (~32 ms) com setor na fila. Com 2+ setores, o
     * dobro. (No PCSX2, V47: com a reserva só no buffer quase vazio, a fila
     * chegou a 4 setores com o buffer secando e a música falhou no meio da
     * luta; com 1/2 do nível a reserva fazia ~40% do trabalho, no VBlank.) */
    {
        unsigned pend = fila_n ? fila_n * 18u - fila_grupo : 0u;   /* grupos na fila */
        unsigned g = 0;
        if (pend > 36u) g = 2u * GRUPOS_VB;
        else if (pend > 18u || (pend && anel_n < NIVEL / 4u)) g = GRUPOS_VB;
        if (g > pend) g = pend;
        if (g) decodificar_fila(g);
    }
    /* V47: envio sem esperar. O que o IOP aceitou do envio anterior sai do
     * começo do buffer; o que ele recusou (fila cheia) vai de novo agora. */
    aceitos = kit_som_concluir();
    if (aceitos < 0) return;                      /* o IOP ainda não respondeu */
    /* Silêncio (sem música) também passa por aqui: o IOP aceitou zeros, não
     * quadros do buffer. Corte (troca de canal, Init...) que esvaziou o
     * buffer: volta a esperar PARTIDA sem contar falta e sem as perguntas ao
     * IOP da conferência. */
    if (tirar_do_buffer(enviou_silencio ? 0u : (unsigned)aceitos) && !anel_n) tocando = 0;
    enviou_silencio = 0;
    if (!tocando && (anel_n >= PARTIDA || (cdda_drenar && anel_n))) tocando = 1;
    if (!anel_n) cdda_drenar = 0;
    if (tocando) {
        if (anel_n) {
            n = anel_n < KIT_SOM_LOTE ? anel_n : KIT_SOM_LOTE;   /* ~1,3 VBlank */
            a = ANEL - anel_ini;
            if (a > n) a = n;
            voo_q = kit_som_mandar(&anel[2u * anel_ini], a, anel, n - a);
        } else {
            /* Buffer do EE vazio é normal (foi tudo para a fila do IOP). Falta
             * de verdade é a fila do IOP também acabando: menos de 1 VBlank de
             * som. Só aí entra silêncio e espera juntar PARTIDA de novo. Só
             * aqui (raro) pergunta ao IOP esperando a resposta. */
            kit_som_livre();
            if (kit_som_na_fila() < SAIDA_HZ / 60u) tocando = 0;
        }
    }
    if (!tocando && kit_som_mandar_silencio(SILENCIO_VB)) enviou_silencio = 1;
#endif
}


/* io.c: leitura de registrador da SPU que a SPU2 responde (V53) */
int ps2_som_spu_leitura(uint32_t off, unsigned w, uint32_t *v)
{
    return ps2_spu_leitura(off, w, v);
}
