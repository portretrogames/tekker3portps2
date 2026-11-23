/* cdrom.c — ver cdrom.h. Referência: nocash psx-spx (CDROM Controller). */
#include "cdrom.h"
#include "io.h"
#include "ram.h"
#include "som.h"     /* V47: música XA */

#include <stdio.h>
#include <string.h>

/* ---------------- disco ---------------- */

#define SETOR      2352u
#define T1_SETORES PS2_CD_T1_SETORES                     /* faixa 1 (dados) */
#define T2_INICIO  (T1_SETORES + 150u)                     /* INDEX 01 da faixa 2 */
#define T3_INICIO  (T1_SETORES + PS2_CD_T2_SETORES + 150u) /* INDEX 01 da faixa 3 */
#define LEADOUT    (T1_SETORES + PS2_CD_T2_SETORES + PS2_CD_T3_SETORES)

static FILE *disco;

/*
 * Acesso ao arquivo .bin. No PS2 real (fileXio carregado) a leitura é
 * ASSÍNCRONA: enquanto o jogo consome um bloco de 32 setores, o IOP já lê o
 * próximo para o outro buffer. Antes cada fread de 75 KB parava o EE ~150 ms
 * (USB 1.1) e o CD ocupava ~41% do tempo na luta. Fora do console (testes no
 * PC, PCSX2 via host:) usa stdio síncrono com a mesma lógica de blocos.
 */
#ifdef _EE
/* o SDK proíbe incluir fileXio_rpc.h junto com a newlib: declarações à mão.
 * Este descritor é só do cdrom (o resto do programa usa a libc normalmente). */
extern int  open(const char *path, int flags, ...);   /* <fcntl.h> colide com 'stat' */
extern int  ps2sdk_get_iop_fd(int fd);
extern int  close(int fd);
extern int  fileXioRead(int fd, void *buf, int size);
extern int  fileXioLseek(int fd, int offset, int whence);
extern void fileXioSetBlockMode(int blocking);
extern int  fileXioWaitAsync(int mode, int *retVal);
#define FXIO_WAIT     0
#define FXIO_NOWAIT   1
#define FXIO_COMPLETE 1
#include "kit/armazenamento.h"
static int fx_fd = -1;
#endif

#define CACHE_SETORES 32u
#define BLOCO_BYTES   (CACHE_SETORES * SETOR)          /* 75264 = 64 * 1176 */
static uint8_t  blocos[2][BLOCO_BYTES] __attribute__((aligned(64)));
static unsigned atual;                                 /* buffer em uso */
static uint32_t cache_ini = 0xFFFFFFFFu, cache_n;      /* setores no buffer atual */
static int      pend;                                  /* leitura em andamento no outro buffer */
static int      pend_feita, pend_bytes;                /* resultado já colhido */
static uint32_t pend_ini;
static uint32_t pos_arquivo = 0xFFFFFFFFu;             /* próximo setor do arquivo */

static int arq_aberto(void)
{
#ifdef _EE
    if (fx_fd >= 0) return 1;
#endif
    return disco != NULL;
}

static int arq_seek(uint32_t lba)
{
#ifdef _EE
    if (fx_fd >= 0)
        return fileXioLseek(fx_fd, (int)(lba * SETOR), SEEK_SET) == (int)(lba * SETOR);
#endif
    return fseek(disco, (long)lba * (long)SETOR, SEEK_SET) == 0;
}

/* começa a ler 'bytes' para buf (no stdio já termina aqui) */
static void arq_iniciar(void *buf, int bytes)
{
    pend = 1; pend_feita = 0;
#ifdef _EE
    if (fx_fd >= 0) {
        fileXioSetBlockMode(FXIO_NOWAIT);
        fileXioRead(fx_fd, buf, bytes);
        fileXioSetBlockMode(FXIO_WAIT);
        return;
    }
#endif
    pend_bytes = (int)fread(buf, 1, (size_t)bytes, disco);
    pend_feita = 1;
}

/* resultado da leitura pendente; bloqueia se 'esperar' */
static int arq_colher(int esperar)
{
    if (pend_feita) return 1;
#ifdef _EE
    if (fx_fd >= 0) {
        int r = 0, c;
        fileXioSetBlockMode(FXIO_NOWAIT);
        if (esperar) { fileXioWaitAsync(FXIO_WAIT, &r); c = FXIO_COMPLETE; }
        else c = fileXioWaitAsync(FXIO_NOWAIT, &r);
        fileXioSetBlockMode(FXIO_WAIT);
        if (c != FXIO_COMPLETE) return 0;
        pend_bytes = r < 0 ? 0 : r;
        pend_feita = 1;
        return 1;
    }
#endif
    (void)esperar;
    return 1;
}

/* 1 se não há leitura do CD em andamento no fileXio (sem esperar): o CD-DA
 * (som.c) lê a faixa 2 só nesses momentos (o fileXio faz um pedido por vez). */
int ps2_cdrom_io_livre(void)
{
    return !pend || pend_feita || arq_colher(0);
}

static void antecipar(void)
{
    uint32_t prox = cache_ini + cache_n;
    uint32_t n;
    if (pend || cache_n != CACHE_SETORES || prox >= T1_SETORES || pos_arquivo != prox) return;
    n = T1_SETORES - prox;
    if (n > CACHE_SETORES) n = CACHE_SETORES;
    pend_ini = prox;
    pos_arquivo = prox + n;
    arq_iniciar(blocos[atual ^ 1u], (int)(n * SETOR));
}

static void cache_invalidar(void)
{
    if (pend) { arq_colher(1); pend = 0; }
    cache_n = 0; cache_ini = 0xFFFFFFFFu; pos_arquivo = 0xFFFFFFFFu;
}

int ps2_cdrom_abrir(const char *pasta)
{
    static const char *nomes[] = {
        "TEKKEN3.BIN",
        "Tekken 3 (USA) (Track 1).bin",
        "../Tekken 3 (USA) (Track 1).bin",
    };
    char caminho[300];
    unsigned i;

    for (i = 0; i < sizeof(nomes) / sizeof(nomes[0]) && !arq_aberto(); i++) {
        snprintf(caminho, sizeof(caminho), "%s%s", pasta, nomes[i]);
#ifdef _EE
        if (kit_arm_filexio_ativo()) {
            {   /* abre pela libc e usa o descritor do IOP por baixo */
                int fd = open(caminho, 0 /* O_RDONLY */);
                fx_fd = fd >= 0 ? ps2sdk_get_iop_fd(fd) : -1;
                if (fd >= 0 && fx_fd < 0) close(fd);     /* não vaza o descritor */
            }
            if (fx_fd < 0) fx_fd = -1;
            else { break; }
            continue;
        }
#endif
        disco = fopen(caminho, "rb");
    }
    {
        const char *achou = arq_aberto() ? pasta : NULL;   /* pasta que abriu o disco */
        if (!arq_aberto()) {
            disco = fopen("mass:/TEKKEN3/TEKKEN3.BIN", "rb");
            if (disco) { achou = "mass:/TEKKEN3/"; }
        }
        /* V59: a faixa de áudio abre agora, com o fileXio sem pedido em andamento */
        if (arq_aberto()) ps2_som_cdda_abrir(achou);
    }
    cache_invalidar();
    return arq_aberto();
}

static int ler_setor(uint32_t lba, uint8_t *buf)
{
    if (!arq_aberto() || lba >= T1_SETORES) {   /* áudio/além do fim: zeros */
        memset(buf, 0, SETOR);
        return 0;
    }
    if (lba - cache_ini >= cache_n) {
        if (pend && lba - pend_ini < CACHE_SETORES) {
            /* próximo bloco já pedido: usa (só espera se ainda não chegou) */
            arq_colher(1);
            pend = 0;
            atual ^= 1u;
            cache_ini = pend_ini;
            cache_n = (uint32_t)pend_bytes / SETOR;
            if (cache_n < CACHE_SETORES) pos_arquivo = 0xFFFFFFFFu;
        } else {
            /* fora da sequência: descarta a antecipação e lê agora */
            uint32_t n = T1_SETORES - lba;
            if (pend) { arq_colher(1); pend = 0; }
            if (n > CACHE_SETORES) n = CACHE_SETORES;
            if (pos_arquivo != lba && !arq_seek(lba)) {
                cache_n = 0; pos_arquivo = 0xFFFFFFFFu;
                memset(buf, 0, SETOR);
                return 0;
            }
            arq_iniciar(blocos[atual], (int)(n * SETOR));
            arq_colher(1);
            pend = 0;
            cache_ini = lba;
            cache_n = (uint32_t)pend_bytes / SETOR;
            pos_arquivo = lba + cache_n;
            if (cache_n < n) pos_arquivo = 0xFFFFFFFFu;
        }
        if (lba - cache_ini >= cache_n) {
            cache_n = 0; pos_arquivo = 0xFFFFFFFFu;
            memset(buf, 0, SETOR);
            return 0;
        }
    }
    memcpy(buf, blocos[atual] + (size_t)(lba - cache_ini) * SETOR, SETOR);
    antecipar();
    return 1;
}

/* 2048 bytes de dados de um setor (modo 1 ou modo 2 forma 1). */
int ps2_cdrom_ler_dados(uint32_t lba, uint8_t *dst)
{
    static uint8_t buf[SETOR];
    if (!ler_setor(lba, buf)) return 0;
    memcpy(dst, buf + (buf[15] == 1u ? 16u : 24u), 2048u);
    return 1;
}

/* ---------------- utilidades ---------------- */

static uint8_t bcd2bin(uint8_t b) { return (uint8_t)((b >> 4) * 10u + (b & 15u)); }
static uint8_t bin2bcd(uint32_t v) { return (uint8_t)(((v / 10u) << 4) | (v % 10u)); }

/* LBA (0 = 00:02:00) -> MSF em BCD */
static void lba_para_msf(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f)
{
    uint32_t t = lba + 150u;
    *m = bin2bcd(t / 4500u);
    *s = bin2bcd((t / 75u) % 60u);
    *f = bin2bcd(t % 75u);
}

/* ---------------- estado do controlador ---------------- */

#define STAT_MOTOR  0x02u
#define STAT_READ   0x20u
#define STAT_SEEK   0x40u
#define STAT_PLAY   0x80u

#define INT1 1u   /* dados prontos */
#define INT2 2u   /* comando concluído (2ª resposta) */
#define INT3 3u   /* 1ª resposta */
#define INT4 4u   /* DataEnd (fim da faixa de áudio) */
#define INT5 5u   /* erro */
#define MODO_AUTOPAUSE 0x02u

#define CICLOS_1A_RESP   50000u
#define CICLOS_SETOR_1X  451584u   /* 33.868.800 / 75 */

typedef struct {
    uint32_t quando;
    uint8_t  tipo, n;
    uint8_t  resp[16];
} Evento;

static Evento   fila[8];
static unsigned n_fila;

static uint8_t  indice, ie, iflag;
static uint8_t  param[16], n_param;
static uint8_t  resp[16], n_resp, pos_resp;
static uint8_t  stat = STAT_MOTOR, modo;
static uint32_t alvo_lba, atual_lba;
static int      setloc_pendente, lendo, ocupado;
static uint32_t prox_setor;
static uint8_t  setor[SETOR], setor_ok;     /* último setor lido */
static uint32_t setor_lba;                  /* LBA do último setor lido */
static int      setor_pedido;               /* jogo já pediu os dados (BFRD)? */
/* BFRD escrito quando ainda não havia setor de dados: fica pendente e é
 * atendido quando o próximo setor de dados chegar (antes ficava sem DRQ para
 * sempre: o libcd não escreve BFRD de novo). Limpo por BFRD=0, nova leitura
 * e reset. */
static int      bfrd_pendente;
static void preparar_fifo(void);
static uint32_t setor_quando;               /* quando o último setor foi lido */
static uint32_t ultima_leitura;             /* V48: última leitura física (dado ou XA) */
static uint8_t  fifo[SETOR];
static uint32_t fifo_n, fifo_pos;
static uint32_t agora;                      /* ciclos PS1 (última atualização) */

void ps2_cdrom_reset(void)
{
    cache_invalidar();
    n_fila = 0; indice = iflag = 0;
    /* A BIOS do PS1 termina o boot com as interrupções do CD habilitadas
     * (IE = 1Fh) e o libcd do Tekken não as reabilita: com IE = 0 a resposta
     * do 1º comando nunca virava IRQ e o boot esperava ~12 s pelo timeout
     * "CD_cw:(CdlNop)" (achado na análise do log pelo ChatGPT). */
    ie = 0x1Fu;
    n_param = n_resp = pos_resp = 0;
    stat = STAT_MOTOR; modo = 0;
    alvo_lba = atual_lba = 0;
    setloc_pendente = lendo = ocupado = 0;
    fifo_n = fifo_pos = 0; setor_ok = 0; setor_pedido = 0; bfrd_pendente = 0;
    ultima_leitura = agora - CICLOS_SETOR_1X;   /* a 1ª leitura não espera */
}

static void agendar(uint32_t atraso, uint8_t tipo, const uint8_t *r, uint8_t n)
{
    Evento *e;
    if (n_fila >= 8) return;
    e = &fila[n_fila++];
    e->quando = agora + atraso;
    e->tipo = tipo;
    e->n = n;
    memcpy(e->resp, r, n);
}

static void agendar_stat(uint32_t atraso, uint8_t tipo)
{
    agendar(atraso, tipo, &stat, 1);
}

/* V59: ver cdrom.h. Como o controlador do PS1: o motor para de tocar e, no
 * fim normal com Autopause, vem DataEnd. */
void ps2_cdrom_cdda_fim(int normal)
{
    if (stat & STAT_PLAY) atual_lba = ps2_som_cdda_lba();
    stat &= (uint8_t)~STAT_PLAY;
    if (normal && (modo & MODO_AUTOPAUSE)) agendar_stat(CICLOS_1A_RESP, INT4);
}

static void remover_int1(void)
{
    unsigned i, j = 0;
    for (i = 0; i < n_fila; i++)
        if (fila[i].tipo != INT1) fila[j++] = fila[i];
    n_fila = j;
}

static uint32_t periodo_setor(void)
{
    return (modo & 0x80u) ? CICLOS_SETOR_1X / 2u : CICLOS_SETOR_1X;
}

static void comecar_leitura(void)
{
    setor_ok = 0;            /* nova leitura: não espera pelo setor antigo */
    setor_pedido = 0;
    bfrd_pendente = 0;
    remover_int1();          /* INT1 da leitura anterior não vale para a nova */
    if (setloc_pendente) { atual_lba = alvo_lba; setloc_pendente = 0; }
    lendo = 1;
    stat = (uint8_t)((stat | STAT_READ) & ~(STAT_PLAY | STAT_SEEK));
    prox_setor = agora + periodo_setor() + CICLOS_1A_RESP;
}

static void comando(uint8_t cmd)
{
    uint8_t r[8];
    uint32_t t;

    ocupado = 1;
    switch (cmd) {
    case 0x01:                                    /* GetStat (Nop) */
        agendar_stat(CICLOS_1A_RESP, INT3);
        break;
    case 0x02:                                    /* Setloc mm ss ff */
        t = bcd2bin(param[0]) * 4500u + bcd2bin(param[1]) * 75u + bcd2bin(param[2]);
        alvo_lba = t >= 150u ? t - 150u : 0u;
        setloc_pendente = 1;
        agendar_stat(CICLOS_1A_RESP, INT3);
        break;
    case 0x03:                                    /* Play (CDDA): som.c toca a faixa (V59) */
        if (setloc_pendente) { atual_lba = alvo_lba; setloc_pendente = 0; }
        if (n_param >= 1u && param[0]) {          /* Play com a faixa (BCD): começo dela */
            uint8_t f = bcd2bin(param[0]);
            atual_lba = f == 2u ? T2_INICIO : f == 3u ? T3_INICIO : 0u;
        }
        lendo = 0; remover_int1();
        stat = (uint8_t)((stat | STAT_PLAY) & ~STAT_READ);
        agendar_stat(CICLOS_1A_RESP, INT3);
        /* depois da resposta: se não há o que tocar, som.c avisa o fim já
         * (ps2_cdrom_cdda_fim tira o STAT_PLAY; o INT4 vem depois do INT3) */
        ps2_som_cd_cmd(cmd, param, atual_lba);
        break;
    case 0x06: case 0x1B:                         /* ReadN / ReadS */
        ps2_som_cd_cmd(cmd, param, 0);            /* V59: para o CD-DA */
        agendar_stat(CICLOS_1A_RESP, INT3);
        comecar_leitura();
        break;
    case 0x07:                                    /* MotorOn */
        stat |= STAT_MOTOR;
        agendar_stat(CICLOS_1A_RESP, INT3);
        agendar_stat(CICLOS_1A_RESP * 4u, INT2);
        break;
    case 0x08:                                    /* Stop */
        if (stat & STAT_PLAY) atual_lba = ps2_som_cdda_lba();   /* V59: onde parou */
        ps2_som_cd_cmd(cmd, param, 0);            /* V59: para o CD-DA */
        lendo = 0; remover_int1();
        agendar_stat(CICLOS_1A_RESP, INT3);
        stat = 0;
        agendar_stat(CICLOS_1A_RESP * 4u, INT2);
        break;
    case 0x09:                                    /* Pause */
        if (stat & STAT_PLAY) atual_lba = ps2_som_cdda_lba();   /* V59: Play retoma daqui */
        ps2_som_cd_cmd(cmd, param, 0);            /* V59: para o CD-DA */
        agendar_stat(CICLOS_1A_RESP, INT3);
        lendo = 0; remover_int1();
        stat &= (uint8_t)~(STAT_READ | STAT_PLAY | STAT_SEEK);
        agendar_stat(CICLOS_1A_RESP + periodo_setor(), INT2);
        break;
    case 0x0A: case 0x1C:                         /* Init / Reset */
        lendo = 0; remover_int1();
        modo = 0;
        ps2_som_cd_cmd(cmd, param, 0);
        stat = STAT_MOTOR;
        agendar_stat(CICLOS_1A_RESP, INT3);
        if (cmd == 0x0A) agendar_stat(CICLOS_1A_RESP * 8u, INT2);
        break;
    case 0x0B: case 0x0C: case 0x0D: case 0x12:   /* Mute/Demute/Setfilter/SetSession */
        if (cmd != 0x12) ps2_som_cd_cmd(cmd, param, 0);   /* V47: mudo e filtro do XA */
        agendar_stat(CICLOS_1A_RESP, INT3);
        if (cmd == 0x12) agendar_stat(CICLOS_1A_RESP * 4u, INT2);
        break;
    case 0x0E:                                    /* Setmode */
        modo = param[0];
        ps2_som_cd_cmd(cmd, param, 0);
        agendar_stat(CICLOS_1A_RESP, INT3);
        break;
    case 0x0F:                                    /* Getparam */
        r[0] = stat; r[1] = modo; r[2] = 0; r[3] = 0; r[4] = 0;
        agendar(CICLOS_1A_RESP, INT3, r, 5);
        break;
    case 0x10:                                    /* GetlocL: cabeçalho do setor */
        agendar(CICLOS_1A_RESP, INT3, setor + 12, 8);
        break;
    case 0x11: {                                  /* GetlocP */
        uint32_t trilha = 1, ini = 0, rel, indice = 1;
        if (stat & STAT_PLAY) atual_lba = ps2_som_cdda_lba();   /* V59: onde o CD-DA está */
        /* faixa pelo INDEX 00 (o pregap de 2 s é da faixa seguinte); no
         * pregap, índice 0 e o tempo contando até o INDEX 01 */
        if (atual_lba >= T3_INICIO - 150u) { trilha = 3; ini = T3_INICIO; }
        else if (atual_lba >= T2_INICIO - 150u) { trilha = 2; ini = T2_INICIO; }
        if (atual_lba < ini) { indice = 0; rel = ini - atual_lba; }
        else rel = atual_lba - ini;
        r[0] = bin2bcd(trilha); r[1] = (uint8_t)indice;
        r[2] = bin2bcd(rel / 4500u); r[3] = bin2bcd((rel / 75u) % 60u);
        r[4] = bin2bcd(rel % 75u);
        lba_para_msf(atual_lba, &r[5], &r[6], &r[7]);
        agendar(CICLOS_1A_RESP, INT3, r, 8);
        break; }
    case 0x13:                                    /* GetTN */
        r[0] = stat; r[1] = 0x01; r[2] = 0x03;
        agendar(CICLOS_1A_RESP, INT3, r, 3);
        break;
    case 0x14: {                                  /* GetTD trilha */
        uint32_t tr = bcd2bin(param[0]), lba;
        uint8_t m, s, f;
        lba = tr == 0 ? LEADOUT : tr == 1 ? 0u : tr == 2 ? T2_INICIO : T3_INICIO;
        lba_para_msf(lba, &m, &s, &f);
        r[0] = stat; r[1] = m; r[2] = s;
        agendar(CICLOS_1A_RESP, INT3, r, 3);
        break; }
    case 0x15: case 0x16:                         /* SeekL / SeekP */
        ps2_som_cd_cmd(cmd, param, 0);            /* V59: para o CD-DA */
        if (setloc_pendente) { atual_lba = alvo_lba; setloc_pendente = 0; }
        lendo = 0; remover_int1();
        stat = (uint8_t)((stat | STAT_SEEK) & ~(STAT_READ | STAT_PLAY));
        agendar_stat(CICLOS_1A_RESP, INT3);
        stat &= (uint8_t)~STAT_SEEK;
        agendar_stat(CICLOS_1A_RESP * 6u, INT2);
        break;
    case 0x19:                                    /* Test */
        if (param[0] == 0x20) {
            r[0] = 0x94; r[1] = 0x09; r[2] = 0x19; r[3] = 0xC0;
            agendar(CICLOS_1A_RESP, INT3, r, 4);
        } else {
            agendar_stat(CICLOS_1A_RESP, INT3);
        }
        break;
    case 0x1A: {                                  /* GetID: disco licenciado NTSC-U */
        static const uint8_t id[8] = {0x02,0x00,0x20,0x00,'S','C','E','A'};
        agendar_stat(CICLOS_1A_RESP, INT3);
        agendar(CICLOS_1A_RESP * 2u, INT2, id, 8);
        break; }
    case 0x1E:                                    /* ReadTOC */
        agendar_stat(CICLOS_1A_RESP, INT3);
        agendar_stat(CICLOS_1A_RESP * 20u, INT2);
        break;
    default:
        r[0] = (uint8_t)(stat | 1u); r[1] = 0x40;
        agendar(CICLOS_1A_RESP, INT5, r, 2);
        break;
    }
    n_param = 0;
}

/* ---------------- tempo ---------------- */

void ps2_cdrom_update(uint32_t ciclos)
{
    agora = ciclos;

    /* leitura contínua: um setor a cada 1/75 s (ou 1/150 s em 2x) */
    if (lendo && (int32_t)(agora - prox_setor) >= 0) {
        int tem_int1 = 0;
        unsigned i;
        for (i = 0; i < n_fila; i++) if (fila[i].tipo == INT1) tem_int1 = 1;
        /* O port roda mais devagar que um PS1 em alguns trechos: o drive
         * espera o jogo reconhecer o setor anterior (INT1 entregue e com
         * ack) antes de ler o próximo, senão um setor se perderia. */
        /* Espera o jogo pedir os dados do setor anterior (o port pode ser
         * mais lento que um PS1), mas não para sempre: se o jogo ignorar um
         * setor (o hardware real simplesmente segue), após 8 períodos o
         * drive entrega o próximo. */
        /* V48: na recuperação de atraso, no máximo 2x a cadência entre duas
         * leituras quaisquer (um setor XA não muda setor_quando, e sem isto
         * o setor seguinte saía na mesma conferência) */
        if (!tem_int1 && iflag == 0 &&
            (uint32_t)(agora - ultima_leitura) >= periodo_setor() / 2u &&
            (!setor_ok ||
             (setor_pedido && (uint32_t)(agora - setor_quando) >= periodo_setor() / 2u) ||
             (!setor_pedido && (uint32_t)(agora - setor_quando) >= 8u * periodo_setor()))) {
            prox_setor += periodo_setor();
            /* V48: se o jogo demorou a atender (o drive esperou), recupera o
             * atraso: o próximo setor sai assim que o jogo pedir este (no
             * máximo 2x mais rápido, pela regra do meio período acima). Antes
             * o horário recomeçava do "agora" e cada demora era perdida de
             * vez: no vídeo da abertura o CD (vídeo e música juntos) andava
             * a ~92% do PS1 nos trechos pesados e o som falhava. Mais de 8
             * setores de atraso (pausa longa): recomeça do agora. */
            if ((int32_t)(agora - prox_setor) >= (int32_t)(8u * periodo_setor()))
                prox_setor = agora + periodo_setor();
            {
                static uint8_t novo[SETOR];
                if (!ler_setor(atual_lba, novo) && atual_lba < T1_SETORES) {
                    uint8_t r[2];
                    r[0] = (uint8_t)(stat | 1u); r[1] = 0x04;      /* erro de leitura */
                    lendo = 0;
                    agendar(0, INT5, r, 2);
                    return;
                }
                atual_lba++;
                ultima_leitura = agora;
                /* setor de áudio XA (submode bit2) com XA ligado: vai para o
                 * SPU (ainda mudo). NÃO substitui o setor de dados pendente:
                 * no console o jogo é mais lento e pede os dados depois que o
                 * próximo setor (XA da música) já passou; antes o XA apagava
                 * o dado e o libcd esperava para sempre (luta travada). */
                if (novo[18] & 0x04u) ps2_som_xa(novo, (modo & 0x40u) != 0);   /* V47 */
                if ((modo & 0x40u) && (novo[18] & 0x04u)) {
                    if (!setor_ok) setor_quando = agora;
                } else {
                    uint8_t st = stat;
                    memcpy(setor, novo, SETOR);
                    setor_lba = atual_lba - 1u;
                    setor_pedido = 0;
                    setor_quando = agora;
                    setor_ok = 1;
                    agendar(0, INT1, &st, 1);
                    if (bfrd_pendente && fifo_pos >= fifo_n) {
                        bfrd_pendente = 0;
                        preparar_fifo();
                    }
                }
            }
        }
    }

    /* entrega o próximo evento quando o anterior já foi reconhecido */
    if (n_fila && iflag == 0 && (int32_t)(agora - fila[0].quando) >= 0) {
        Evento e = fila[0];
        memmove(fila, fila + 1, (n_fila - 1u) * sizeof(Evento));
        n_fila--;
        memcpy(resp, e.resp, e.n);
        n_resp = e.n; pos_resp = 0;
        iflag = e.tipo;
        if (e.tipo == INT3) ocupado = 0;
        if (iflag & ie & 0x1Fu)
            ps2_irq_raise(IRQ_CDROM);
    }
}

/* ---------------- registradores ---------------- */

static uint8_t status_reg(void)
{
    uint8_t s = indice;
    if (n_param == 0) s |= 0x08;                 /* PRMEMPT */
    if (n_param < 16) s |= 0x10;                 /* PRMWRDY */
    if (pos_resp < n_resp) s |= 0x20;            /* RSLRRDY */
    if (fifo_pos < fifo_n) s |= 0x40;            /* DRQSTS */
    if (ocupado) s |= 0x80;                      /* BUSYSTS */
    return s;
}

uint8_t ps2_cdrom_read(uint32_t reg)
{
    switch (reg & 3u) {
    case 0: return status_reg();
    case 1: return pos_resp < n_resp ? resp[pos_resp++] : 0;
    case 2: return fifo_pos < fifo_n ? fifo[fifo_pos++] : 0;
    case 3: return (indice & 1u) ? (uint8_t)(0xE0u | iflag) : (uint8_t)(0xE0u | ie);
    }
    return 0;
}

static void preparar_fifo(void)
{
    if (!setor_pedido) setor_quando = agora;   /* conta a partir do pedido */
    setor_pedido = 1;
    if (!setor_ok) { fifo_n = fifo_pos = 0; bfrd_pendente = 1; return; }
    bfrd_pendente = 0;
    if (modo & 0x20u) {                          /* setor inteiro (0x924) */
        memcpy(fifo, setor + 12, 0x924u); fifo_n = 0x924u;
    } else {                                     /* só dados (0x800, Mode2 Form1) */
        memcpy(fifo, setor + 24, 0x800u); fifo_n = 0x800u;
    }
    fifo_pos = 0;
}

void ps2_cdrom_write(uint32_t reg, uint8_t v)
{
    reg &= 3u;
    if (reg == 0) { indice = v & 3u; return; }
    switch ((reg << 2) | indice) {
    case (1u << 2) | 0u: comando(v); break;                       /* comando */
    case (2u << 2) | 0u: if (n_param < 16) param[n_param++] = v; break;
    case (2u << 2) | 1u: {                                         /* IE */
        /* A linha de IRQ do CD é (IF & IE). Se já havia resposta pendente
         * quando o jogo habilita a interrupção, ela dispara agora (antes a
         * resposta do 1º comando, dada com IE=0, nunca virava IRQ e o boot
         * esperava ~12 s pelo timeout "CD_cw:(CdlNop)"). */
        uint8_t antes = iflag & ie & 0x1Fu;
        ie = v & 0x1Fu;
        if (!antes && (iflag & ie & 0x1Fu)) ps2_irq_raise(IRQ_CDROM);
        break;
    }
    case (3u << 2) | 0u:                                          /* pedido */
        if (v & 0x80u) { if (fifo_pos >= fifo_n) preparar_fifo(); }
        else { fifo_n = fifo_pos = 0; bfrd_pendente = 0; }
        break;
    case (3u << 2) | 1u:                                          /* IF: ack */
        iflag &= (uint8_t)~(v & 0x1Fu);
        if (v & 0x40u) n_param = 0;
        if (iflag == 0) n_resp = pos_resp = 0;
        break;
    default: break;                                               /* volume etc. */
    }
}

void ps2_cdrom_dma(uint32_t madr, uint32_t palavras)
{
    uint32_t a = madr & 0x1FFFFCu, i;
    for (i = 0; i < palavras; i++) {
        uint32_t w = 0, k;
        for (k = 0; k < 4u; k++)
            w |= (uint32_t)(fifo_pos < fifo_n ? fifo[fifo_pos++] : 0u) << (8u * k);
        ps2_ram_set32(a, w);
        a = (a + 4u) & 0x1FFFFCu;
    }
}
