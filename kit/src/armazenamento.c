/* kit/src/armazenamento.c — ver kit/armazenamento.h. */
#include "kit/armazenamento.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include "kit/diagnostico.h"     /* kit_relogio, kit_ms_desde */
extern int fileXioInit(void);     /* o header do fileXio proíbe uso junto da newlib */

static char mensagens[1024];
static int  filexio_ok;

static void msg(const char *fmt, ...)
{
    char linha[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(linha, sizeof(linha), fmt, ap);
    va_end(ap);
    fputs(linha, stdout);
    if (strlen(mensagens) + strlen(linha) < sizeof(mensagens))
        strcat(mensagens, linha);
}

/* gerados por bin2c a partir de $(PS2SDK)/iop/irx (kit/kit.mk) */
extern unsigned char iomanX_irx[], fileXio_irx[], usbd_irx[], bdm_irx[],
                     bdmfs_fatfs_irx[], usbmass_bd_irx[];
extern unsigned int size_iomanX_irx, size_fileXio_irx, size_usbd_irx, size_bdm_irx,
                    size_bdmfs_fatfs_irx, size_usbmass_bd_irx;

static int carregar(const char *nome, unsigned char *irx, unsigned int tam)
{
    int res = 0, id = SifExecModuleBuffer(irx, tam, 0, NULL, &res);
    if (id < 0 || res == 1) {           /* res 1 = NO_RESIDENT_END: falhou */
        msg("iop: %s falhou (id %d, res %d)\n", nome, id, res);
        return 0;
    }
    msg("iop: %s carregado\n", nome);
    return 1;
}

/* kit_ms_desde vale para menos de 14 s: espera em fatias de até 1 s */
static void esperar_ms(unsigned ms)
{
    while (ms) {
        unsigned f = ms < 1000u ? ms : 1000u;
        uint32_t ini = kit_relogio();
        while (kit_ms_desde(ini) < f) {}
        ms -= f;
    }
}

/* Soma o tempo desde *ini em *passado (relendo a cada <1 s, sem estouro) e
 * diz se o prazo acabou. */
static int prazo_esgotado(uint32_t *ini, unsigned *passado, unsigned prazo_ms)
{
    unsigned dt = kit_ms_desde(*ini);
    if (dt >= 1000u) { *passado += dt; *ini = kit_relogio(); dt = 0; }
    return *passado + dt >= prazo_ms;
}

/* "host:" ou "host0:".."host9:" (o PCSX2 passa host:/caminho/do/elf) */
static int eh_host(const char *s)
{
    if (!s || strncmp(s, "host", 4) != 0) return 0;
    s += 4;
    if (*s >= '0' && *s <= '9') s++;
    return *s == ':';
}

/* 0.2.1: reset e sincronização do IOP com prazo (antes laços sem fim).
 * O tempo é somado em ms (sem estouro para qualquer prazo). */
static int iop_reiniciar(unsigned prazo_ms)
{
    unsigned passado = 0;
    uint32_t ini = kit_relogio();
    while (!SifIopReset("", 0))
        if (prazo_esgotado(&ini, &passado, prazo_ms)) { msg("iop: SifIopReset sem resposta\n"); return 0; }
    while (!SifIopSync())
        if (prazo_esgotado(&ini, &passado, prazo_ms)) { msg("iop: SifIopSync sem resposta\n"); return 0; }
    return 1;
}

int kit_arm_preparar(const char *argv0, int forcar_usb)
{
    if (!forcar_usb && eh_host(argv0)) {
        msg("iop: rodando de host: (PCSX2), IOP mantido\n");
        return KIT_ARM_HOST;
    }
    msg("iop: reiniciando o IOP e carregando USB (FAT32/exFAT)\n");
    SifInitRpc(0);
    if (!iop_reiniciar(10000u)) return KIT_ARM_ERRO_MODULO;
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();

    if (!carregar("iomanX", iomanX_irx, size_iomanX_irx) ||
        !carregar("fileXio", fileXio_irx, size_fileXio_irx) ||
        !carregar("usbd", usbd_irx, size_usbd_irx) ||
        !carregar("bdm", bdm_irx, size_bdm_irx) ||
        !carregar("bdmfs_fatfs", bdmfs_fatfs_irx, size_bdmfs_fatfs_irx) ||
        !carregar("usbmass_bd", usbmass_bd_irx, size_usbmass_bd_irx))
        return KIT_ARM_ERRO_MODULO;
    if (fileXioInit() < 0) {
        msg("iop: fileXioInit falhou\n");
        return KIT_ARM_ERRO_FILEXIO;
    }
    filexio_ok = 1;
    return KIT_ARM_OK;
}

int kit_arm_esperar(const char *const *caminhos, unsigned timeout_ms,
                    char *achado, size_t tam)
{
    unsigned t;
    for (t = 0;;) {
        unsigned k;
        for (k = 0; caminhos && caminhos[k]; k++) {
            FILE *f = fopen(caminhos[k], "rb");
            if (f) {
                fclose(f);
                msg("iop: arquivo pronto (%u ms): %s\n", t, caminhos[k]);
                if (achado && tam) {
                    strncpy(achado, caminhos[k], tam - 1u);
                    achado[tam - 1u] = '\0';
                }
                return KIT_ARM_OK;
            }
        }
        if (t >= timeout_ms) break;            /* última volta: sem espera depois */
        {   /* espera 100 ms ou o resto do prazo; a última volta cai no prazo */
            unsigned passo = timeout_ms - t < 100u ? timeout_ms - t : 100u;
            esperar_ms(passo);
            t += passo;
        }
    }
    msg("iop: nenhum arquivo encontrado em %u ms\n", timeout_ms);
    return KIT_ARM_NAO_ACHOU;
}

int kit_arm_pasta_do_elf(const char *argv0, char *saida, size_t tam)
{
    const char *b;
    size_t n;
    if (!argv0 || !tam) return 0;
    b = strrchr(argv0, '/');
    if (!b) b = strrchr(argv0, '\\');
    if (!b) b = strchr(argv0, ':');
    if (!b) return 0;
    n = (size_t)(b - argv0) + 1u;
    if (n >= tam) return 0;
    memcpy(saida, argv0, n);
    saida[n] = '\0';
    return 1;
}

int kit_arm_filexio_ativo(void) { return filexio_ok; }
const char *kit_arm_mensagens(void) { return mensagens; }
