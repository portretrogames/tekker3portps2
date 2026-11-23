/*
 * stubs.c — os símbolos de docs/runtime_api.txt que ainda não têm
 * implementação real (Fase 2).
 *
 * Regra: o que pode seguir em frente segue com valor neutro; o que deixaria o
 * jogo em estado errado (syscall desconhecida, salto sem código) PARA a
 * execução.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "psx_runtime.h"
#include "dirty_ram_interp.h"
#include "gpu.h"
#include "load_accel.h"
#include "bios_hle.h"

#define AVISO() do { } while (0)

static void parar(const char *motivo, CPUState *cpu, uint32_t endereco)
{
    (void)motivo; (void)cpu; (void)endereco;
    abort();
}

/* Variáveis de depuração exigidas pelo código gerado. */
uint32_t g_debug_last_store_pc;
volatile uint32_t g_psx_last_fn_entry;
int g_psx_cps_mode;

/* dirty_ram_text_native_ok_ranges_from: ver codigo.c */

/* GTE (Fase 4): por enquanto não calcula nada. */

/* Interrupções/VSync (Fase 2, próxima etapa). */
/* Chamado em quase todo bloco: só olha o relógio a cada 256 chamadas. */
int g_ps2_contagem_irq = 1;
void ps2_irq_poll_lento(CPUState *cpu, uint32_t pc)
{
    extern int ps2_bios_em_excecao(void);
    g_ps2_contagem_irq = 256;
    ps2_irq_poll(cpu, pc);
}
void psx_check_interrupts_at(CPUState *cpu, uint32_t pc)
{
    if (--g_ps2_contagem_irq > 0) return;
    g_ps2_contagem_irq = 256;
    ps2_irq_poll(cpu, pc);
}
void psx_check_interrupts_dispatch_entry(CPUState *cpu, uint32_t pc)
{
    psx_check_interrupts_at(cpu, pc);
}
int psx_vsync_query_hle_try(CPUState *cpu, uint32_t addr)
{ (void)cpu; (void)addr; return 0; }
void psx_restore_state_escape(void)
{ AVISO(); }

/*
 * Instrução SYSCALL do PS1 (a0 = função):
 *   1 = EnterCriticalSection (desliga interrupções, devolve o IEc antigo)
 *   2 = ExitCriticalSection  (liga interrupções)
 *   3 = ChangeThread / ReturnFromException (ainda não)
 * Retorno 0 + cpu->pc = 0: o código gerado segue para a instrução
 * seguinte (convenção do psxrecomp, ver traps.c da referência).
 */
int psx_syscall(CPUState *cpu, uint32_t code)
{
    uint32_t sr = cpu->cop0[12];
    (void)code;
    switch (cpu->gpr[4]) {
    case 1:
        cpu->cop0[12] = sr & ~1u;
        cpu->gpr[2] = sr & 1u;
        cpu->pc = 0;
        return 0;
    case 2:
        cpu->cop0[12] = sr | 0x0401u;
        cpu->gpr[2] = 0;
        cpu->pc = 0;
        return 0;
    }
    parar("syscall nao implementado", cpu, cpu->pc);
    return 0;
}

/*
 * Salto para endereço sem código compilado. Acontece quando o recompilador
 * viu DADOS no executável original num endereço onde, em tempo de execução,
 * o jogo carrega código do disco (overlay): a "função" gerada só chama isto.
 * Em vez de parar, marca o endereço para o laço de despacho (main.c)
 * executar pelo interpretador.
 */
uint32_t g_ps2_interpretar_em;

void psx_unknown_dispatch(CPUState *cpu, uint32_t addr, uint32_t phys)
{
    (void)phys;
    if (addr == 0u)
        parar("salto para o endereco 0", cpu, addr);
    g_ps2_interpretar_em = addr;
    cpu->pc = addr;
}

/* Widescreen (Fase 5): comportamento original 4:3. */
int psx_ws_background_cull_bltz(uint32_t v)
{ return (int32_t)v < 0; }
int psx_ws_background_cull_margin(void)
{ return 0; }
void psx_ws_background_packet_tag(uint32_t addr)
{ (void)addr; }
int psx_ws_cull_sltiu(uint32_t sx, uint32_t imm)
{ return sx < imm; }
uint32_t psx_ws_full_angle_widen(uint32_t vanilla)
{ return vanilla; }
