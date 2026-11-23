/*
 * main.c — ponto de entrada do port do Tekken 3 no PS2.
 *
 * 1. Prepara a memória do PS1 (RAM 2 MB + scratchpad).
 * 2. Carrega o executável do jogo (SLUS_004.02, formato PS-X EXE) na RAM.
 *    Procura o arquivo na mesma pasta do ELF (host: no PCSX2, mass: no PS2
 *    real pelo USB) e depois em host: e mass:/TEKKEN3/.
 * 3. Inicia os registradores como a BIOS do PS1 faria (PC, GP, SP/FP).
 * 4. Entra no código recompilado pelo dispatch.
 *
 * A execução para quando encontra código ou hardware não suportado.
 */
#include "abertura.h"
#include "kit/som.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include "cpu_state.h"
#include "memory.h"
#include "ram.h"
#include "quente.h"
#include "bios_hle.h"
#include "cdrom.h"
#include "interp.h"
#include "video_gs.h"
#include "jogo.h"
#include "kit/controle.h"
#include "kit/armazenamento.h"
#include "kit/diagnostico.h"

extern int psx_dispatch_game_compiled(CPUState *cpu, uint32_t addr);

/* Global: o CPUState é grande e não deve ficar na pilha do EE.
 * V56: é um apelido para dentro do bloco quente de ram.c (ram.h: lugar fixo
 * no cache de dados). */
extern CPUState g_ps2_cpu;
typedef char cpu_cabe_no_bloco[(PS2_CPU_NO_CACHE + sizeof(CPUState) <= PS2_COD_PAG_NO_CACHE) ? 1 : -1];

#define despachar(cpu, alvo) psx_dispatch_game_compiled((cpu), (alvo))

#define EXE_NOME        "SLUS_004.02"
#define EXE_CABECALHO   2048u

typedef struct {
    uint32_t pc0, gp0, t_addr, t_size, s_addr, s_size;
} PsxExeInfo;

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Monta "<pasta do ELF>SLUS_004.02" a partir de argv[0]. */
static int caminho_ao_lado_do_elf(const char *argv0, char *out, size_t tam)
{
    const char *fim = NULL;
    const char *p;
    size_t n;

    if (argv0 == NULL || argv0[0] == '\0')
        return 0;
    for (p = argv0; *p; p++)
        if (*p == '/' || *p == '\\' || *p == ':')
            fim = p;
    if (fim == NULL)
        return 0;
    n = (size_t)(fim - argv0) + 1u;
    if (n + sizeof(EXE_NOME) > tam)
        return 0;
    memcpy(out, argv0, n);
    memcpy(out + n, EXE_NOME, sizeof(EXE_NOME));
    return 1;
}

static int carregar_exe(const char *caminho, PsxExeInfo *info)
{
    uint8_t cab[EXE_CABECALHO];
    uint32_t base;
    FILE *f = fopen(caminho, "rb");

    if (f == NULL) {
        return 0;
    }
    if (fread(cab, 1, sizeof(cab), f) != sizeof(cab) ||
        memcmp(cab, "PS-X EXE", 8) != 0) {
        fclose(f);
        return 0;
    }
    info->pc0    = le32(cab + 0x10);
    info->gp0    = le32(cab + 0x14);
    info->t_addr = le32(cab + 0x18);
    info->t_size = le32(cab + 0x1C);
    info->s_addr = le32(cab + 0x30);
    info->s_size = le32(cab + 0x34);

    base = info->t_addr & PS2_RAM_MASK;
    if (!ps2_ram_hit(info->t_addr) || info->t_size > PS2_RAM_SIZE - base) {
        fclose(f);
        return 0;
    }
    if (fread(ps2_ram_data() + base, 1, info->t_size, f) != info->t_size) {
        fclose(f);
        return 0;
    }
    fclose(f);
    {
        extern void ps2_cod_registrar(uint32_t endereco, uint32_t tamanho);
        ps2_cod_registrar(info->t_addr, info->t_size);
        { extern void ps2_despacho_paginas_de_codigo(void); ps2_despacho_paginas_de_codigo(); }
    }
    return 1;
}

/*
 * Sem SLUS_004.02 no pendrive: lê o executável de dentro da imagem do disco
 * (ISO9660 da faixa 1). Assim basta copiar o .bin do jogo.
 */
/* Procura 'nome' (sem ";1", maiúsculas) num diretório ISO9660. */
static int iso_achar(uint32_t dir_lba, uint32_t dir_tam, const char *nome, size_t nlen,
                     uint32_t *lba, uint32_t *tam)
{
    static uint8_t s[2048];
    uint32_t pos;
    if (dir_tam > 256u * 2048u) dir_tam = 256u * 2048u;   /* diretório corrompido */
    for (pos = 0; pos < dir_tam; pos += 2048u) {
        uint32_t o = 0;
        if (!ps2_cdrom_ler_dados(dir_lba + pos / 2048u, s)) return 0;
        while (o < 2048u && s[o]) {
            uint32_t reg = s[o];                /* tamanho do registro */
            uint8_t n;
            /* registro tem de caber no setor e conter o nome inteiro */
            if (reg < 34u || reg > 2048u - o) break;
            n = s[o + 32];
            if (33u + n > reg) break;
            if (nlen && n >= nlen && memcmp(s + o + 33, nome, nlen) == 0 &&
                (n == nlen || s[o + 33 + nlen] == ';')) {
                *lba = le32(s + o + 2);
                *tam = le32(s + o + 10);
                return 1;
            }
            o += s[o];
        }
    }
    return 0;
}

static int carregar_exe_do_disco(PsxExeInfo *info)
{
    static uint8_t setor[2048];
    char boot[128] = "";
    uint32_t dir_lba, dir_tam, lba = 0, tam = 0, base, i;
    uint8_t *dst;
    const char *p;
    if (!ps2_cdrom_ler_dados(16, setor) || memcmp(setor + 1, "CD001", 5) != 0) {
        return 0;
    }
    dir_lba = le32(setor + 156 + 2);
    dir_tam = le32(setor + 156 + 10);
    /* SYSTEM.CNF: BOOT = cdrom:\PASTA\ARQUIVO;1 */
    if (iso_achar(dir_lba, dir_tam, "SYSTEM.CNF", 10, &lba, &tam) &&
        ps2_cdrom_ler_dados(lba, setor)) {
        static char cnf[2049];                  /* o setor não tem NUL no fim */
        const char *b;
        memcpy(cnf, setor, 2048); cnf[tam < 2048u ? tam : 2048u] = '\0';
        b = strstr(cnf, "BOOT");
        if (b && (b = strchr(b, ':')) != NULL) {
            size_t k = 0;
            b++;
            while (*b == '\\' || *b == '/') b++;
            while (*b && *b != ';' && *b != '\r' && *b != '\n' && *b != ' ' && k + 1 < sizeof(boot))
                boot[k++] = *b++;
            boot[k] = '\0';
        }
    }
    if (!boot[0]) strcpy(boot, EXE_NOME);
    /* percorre as pastas */
    p = boot;
    lba = 0;
    for (;;) {
        const char *fim = p;
        while (*fim && *fim != '\\' && *fim != '/') fim++;
        if (!iso_achar(dir_lba, dir_tam, p, (size_t)(fim - p), &lba, &tam)) { lba = 0; break; }
        if (!*fim) break;
        dir_lba = lba; dir_tam = tam;
        p = fim + 1;
    }
    if (!lba) { return 0; }
    if (!ps2_cdrom_ler_dados(lba, setor) || memcmp(setor, "PS-X EXE", 8) != 0) return 0;
    info->pc0    = le32(setor + 0x10);
    info->gp0    = le32(setor + 0x14);
    info->t_addr = le32(setor + 0x18);
    info->t_size = le32(setor + 0x1C);
    info->s_addr = le32(setor + 0x30);
    info->s_size = le32(setor + 0x34);
    base = info->t_addr & PS2_RAM_MASK;
    if (!ps2_ram_hit(info->t_addr) || info->t_size > PS2_RAM_SIZE - base ||
        tam < EXE_CABECALHO || info->t_size > tam - EXE_CABECALHO)
        return 0;
    /* corpo começa em 0x800 (2º setor do arquivo) */
    dst = ps2_ram_data() + base;
    for (i = 0; i < info->t_size; i += 2048u) {
        uint32_t n = info->t_size - i < 2048u ? info->t_size - i : 2048u;
        if (!ps2_cdrom_ler_dados(lba + 1u + i / 2048u, setor)) return 0;
        memcpy(dst + i, setor, n);
    }
    {
        extern void ps2_cod_registrar(uint32_t endereco, uint32_t tamanho);
        ps2_cod_registrar(info->t_addr, info->t_size);
        { extern void ps2_despacho_paginas_de_codigo(void); ps2_despacho_paginas_de_codigo(); }
    }
    return 1;
}


int main(int argc, char **argv)
{
    static const char *alternativas[] = {
        "host:" EXE_NOME,
        "mass:/TEKKEN3/" EXE_NOME,
    };
    char caminho[256];
    PsxExeInfo info;
    CPUState *cpu = &g_ps2_cpu;
    uint32_t sp;
    int ok = 0;
    unsigned i;


    /* PS2 real: reinicia o IOP e prepara o USB antes de tudo (kit) */
    {
        const int forcar = 0;
        static const char *const nomes[] = { JOGO_ARQUIVOS, NULL };
        char pasta_elf[200] = JOGO_PASTA_USB;
        const char *argv0 = argc > 0 ? argv[0] : NULL;
        int r;

        kit_arm_pasta_do_elf(argv0, pasta_elf, sizeof(pasta_elf));
        r = kit_arm_preparar(argv0, forcar);
        if (r == KIT_ARM_OK) {
            /* o pendrive leva alguns segundos para montar */
            enum { N = sizeof(nomes) / sizeof(nomes[0]) - 1 };
            static char buf[2 * N][256];
            const char *caminhos[2 * N + 1];
            unsigned k;
            for (k = 0; k < N; k++) {
                snprintf(buf[2 * k], sizeof(buf[0]), "%s%s", pasta_elf, nomes[k]);
                snprintf(buf[2 * k + 1], sizeof(buf[0]), "%s%s", JOGO_PASTA_USB, nomes[k]);
                caminhos[2 * k] = buf[2 * k];
                caminhos[2 * k + 1] = buf[2 * k + 1];
            }
            caminhos[2 * N] = NULL;
            kit_arm_esperar(caminhos, 10000u, NULL, 0);
        }
        /* sem log: o que ainda sair pelo stdout (bibliotecas) é descartado;
         * pela rede cada linha esperava o PC */
        kit_log_silenciar();
        /* V47: som pelo audsrv (freesd + audsrv embutidos), 48 kHz estéreo */
        kit_som_iniciar(48000);
    }

    memset(cpu, 0, sizeof(*cpu));
    ps2_memory_init(cpu);               /* zera RAM e liga cpu->read/write */
    if (!ps2_video_init()) return 1;
    kit_controle_iniciar(2);            /* V59: portas 1 e 2; sem controle o jogo segue mesmo assim */
    { extern void ps2_cartao_iniciar(void); ps2_cartao_iniciar(); }   /* V59: memory card */
    ps2_abertura(JOGO_VERSAO_NUM);      /* V59: telas de início (abertura.c) */

    /* Disco primeiro: procura a imagem na pasta do ELF (ver cdrom.c). */
    {
        char pasta[256] = "host:";
        if (argc > 0 && caminho_ao_lado_do_elf(argv[0], caminho, sizeof(caminho))) {
            size_t n = strlen(caminho) - (sizeof(EXE_NOME) - 1u);
            if (n >= sizeof(pasta)) n = sizeof(pasta) - 1u;
            memcpy(pasta, caminho, n);
            pasta[n] = '\0';
        }
        /* V53: sem o disco ao lado do ELF (ex.: ELF de teste noutra pasta do
         * OPL), tenta a pasta padrão do jogo no pendrive */
        if (!ps2_cdrom_abrir(pasta) && !ps2_cdrom_abrir(JOGO_PASTA_USB))
            return 1;
    }

    /* Executável: arquivo ao lado do ELF, alternativas, ou de dentro do disco */
    if (argc > 0 && caminho_ao_lado_do_elf(argv[0], caminho, sizeof(caminho)))
        ok = carregar_exe(caminho, &info);
    for (i = 0; !ok && i < sizeof(alternativas) / sizeof(alternativas[0]); i++)
        ok = carregar_exe(alternativas[i], &info);
    if (!ok) ok = carregar_exe_do_disco(&info);
    if (!ok) {
        return 1;
    }

    /* Registradores iniciais, como a BIOS do PS1 deixa ao chamar o jogo. */
    sp = info.s_addr ? info.s_addr + info.s_size : 0x801FFFF0u;
    cpu->gpr[28] = info.gp0;   /* gp */
    cpu->gpr[29] = sp;         /* sp */
    cpu->gpr[30] = sp;         /* fp */
    cpu->pc = info.pc0;


    /*
     * Laço de despacho (trampolim CPS). O código gerado não chama a próxima
     * função direto: cada função termina gravando o próximo endereço em
     * cpu->pc ("CPS jal" / "CPS: jr $ra") e retorna. Este laço continua a
     * execução a partir de cpu->pc até cair num endereço sem código
     * compilado (BIOS, overlay...), quando o runtime para.
     */
    /* Volta aqui (longjmp) quando uma interrupção é entregue ao jogo:
     * cpu->pc já aponta para o handler. */
    /* V56: pilha do EE em posição fixa no cache de dados (quente.h). Antes do
     * setjmp: a volta por longjmp restaura esta mesma posição. */
    PS2_PILHA_EM(PS2_PILHA_JOGO);
    (void)setjmp(g_ps2_volta_ao_laco);
    for (;;) {
        uint32_t alvo = cpu->pc;
        if (!despachar(cpu, alvo) &&
            !ps2_bios_call(cpu, alvo) &&
            !ps2_interp_run(cpu)) {
            return 1;          /* endereço fora do código conhecido */
        }
        /* A função "compilada" era um stub de dado: interpreta (overlay). */
        if (g_ps2_interpretar_em != 0u && cpu->pc == g_ps2_interpretar_em) {
            g_ps2_interpretar_em = 0u;
            if (!ps2_interp_run(cpu)) {
                return 1;      /* não é RAM executável */
            }
        }
    }
}
