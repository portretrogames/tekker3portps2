/*
 * fast_memory.h — caminho rápido de memória para o código GERADO.
 *
 * Incluído automaticamente (gcc -include) só nos arquivos de gerado/,
 * ANTES de qualquer header do psxrecomp. Não editar gerado/: tudo é feito
 * aqui com macros.
 *
 * - Leituras: psx_read_half/psx_read_word/psx_cyc_load_byte e (V46)
 *   psx_cyc_load_word/psx_cyc_load_half viram funções inline que acessam a
 *   RAM direto e só chamam a função real (memory.c) para o resto. Até a V45
 *   o LW/LH do jogo (psx_cyc_load_word/half) ainda chamava memory.c: ver
 *   ps2_fast_cyc_load_word.
 * - Leituras e escritas por ponteiro ("cpu->write_word(addr, v)",
 *   "cpu->read_word(addr)" em psx_swl/psx_swr) viram ps2_fast_* em
 *   tools/transformar_gerado.py, inclusive na cópia de SLUS_004.02_decls.h.
 */
#ifndef PS2_FAST_MEMORY_H
#define PS2_FAST_MEMORY_H

#include <stdint.h>
#include "ram.h"

struct CPUState;

/*
 * Checagem de interrupção em todo desvio do código gerado: antes era uma
 * chamada de função (stubs.c) por desvio; agora só um decremento em linha,
 * e a função só é chamada a cada 256 desvios. A declaração do psxrecomp que
 * vem depois (sem "static") herda a ligação interna desta, o que o C permite.
 */
extern int g_ps2_contagem_irq;
extern uint32_t g_ps2_ver_ok[];
/* V39: perfil por função também nas chamadas C diretas da V33. Sem isto o
 * tempo de uma função chamada direto ia para a última função DESPACHADA
 * (achado A1 da revisão V37). Custa 2 leituras do relógio por chamada:
 * só em build de medida -> make GEN_OPT="-O2 -DPS2_PROF_DIRETO=1". */
#ifndef PS2_PROF_DIRETO
#define PS2_PROF_DIRETO 0
#endif
#if PS2_PROF_DIRETO && defined(_EE)
extern uint32_t g_ps2_prof_fn[], g_ps2_prof_t, g_ps2_prof_ult;
static inline __attribute__((always_inline)) uint32_t ps2_prof_d_troca(uint32_t novo)
{
    uint32_t c, ant = g_ps2_prof_ult;
    __asm__ volatile("mfc0 %0, $9" : "=r"(c));
    g_ps2_prof_fn[ant] += c - g_ps2_prof_t;
    g_ps2_prof_t = c;
    g_ps2_prof_ult = novo;
    return ant;
}
#define PS2_PROF_D_ENTRA(i) uint32_t _pu = ps2_prof_d_troca(i);
#define PS2_PROF_D_SAI      (void)ps2_prof_d_troca(_pu);
#else
#define PS2_PROF_D_ENTRA(i)
#define PS2_PROF_D_SAI
#endif     /* V33: entrada do despacho validada nesta versão */
void ps2_irq_poll_lento(struct CPUState *cpu, uint32_t pc);
static inline __attribute__((always_inline)) void psx_check_interrupts_at(struct CPUState *cpu, uint32_t pc)
{
    if (__builtin_expect(--g_ps2_contagem_irq <= 0, 0))
        ps2_irq_poll_lento(cpu, pc);
}

/* Caminho lento (memory.c): scratchpad, BIOS e I/O. */
uint8_t  psx_read_byte(uint32_t address);
uint16_t psx_read_half(uint32_t address);
uint32_t psx_read_word(uint32_t address);
void psx_write_byte(uint32_t address, uint8_t value);
void psx_write_half(uint32_t address, uint16_t value);
void psx_write_word(uint32_t address, uint32_t value);

/*
 * Scratchpad (1 KB em 0x1F800000, só KUSEG e KSEG0 — não KSEG1): o Tekken
 * usa muito (matrizes, esqueleto). Antes passava pela função geral de
 * memory.c; agora é acesso direto. Mesmo mapeamento de memory.c.
 */
/* V21: desligado por padrão. Em linha, cada leitura/escrita do código gerado
 * ganha mais um teste e o código do jogo cresceu de 8,4 para 11 MB — o
 * cache de instruções do EE (16 KB) é o limite provável no console.
 * Ligue com -DPS2_SPAD_INLINE=1 para comparar. */
/* g_ps2_scratchpad: declarado em ram.h (V56) */
/* V25: de volta, com teste mais curto. O perfil por função mostrou que as
 * funções 3D mais pesadas (80036cac...) trabalham quase só no scratchpad, e
 * cada acesso lá era uma chamada de função (memory.c). O teste ignora o
 * segmento: KSEG1 (0xBF800000) não tem scratchpad no PS1 e o jogo não usa. */
#ifndef PS2_SPAD_INLINE
#define PS2_SPAD_INLINE 1
#endif
#if PS2_SPAD_INLINE
static inline __attribute__((always_inline)) int ps2_spad_hit(uint32_t a)
{
    return (a & 0x1FFFFC00u) == 0x1F800000u;
}
#else
#define ps2_spad_hit(a) 0
#endif
#define PS2_SPAD(a) (&g_ps2_scratchpad[(a) & 0x3FFu])

/*
 * V46: RAM em KSEG0 (0x80000000-0x807FFFFF: 2 MB e espelhos), onde o jogo
 * lê e grava quase sempre, com um teste só (3 instruções). Antes o teste
 * cobria os três segmentos com RAM (KUSEG, KSEG0, KSEG1): 8 instruções e 2
 * desvios em todo acesso do código gerado. KUSEG/KSEG1 continuam certos:
 * vão para psx_read_* / psx_write_* (memory.c), que começam pelo mesmo
 * ps2_ram_hit() completo de antes. Scratchpad segue em linha (V25).
 */
static inline __attribute__((always_inline)) int ps2_kseg0_ram(uint32_t a)
{
    return (a >> 23) == (0x80000000u >> 23);
}

static inline __attribute__((always_inline)) uint8_t ps2_fast_read_byte(uint32_t a)
{
    if (__builtin_expect(ps2_kseg0_ram(a), 1)) return ps2_ram_get8(a);
    if (ps2_spad_hit(a)) return *PS2_SPAD(a);
    return psx_read_byte(a);
}
static inline __attribute__((always_inline)) uint16_t ps2_fast_read_half(uint32_t a)
{
    if (__builtin_expect(ps2_kseg0_ram(a), 1)) return ps2_ram_get16(a);
    if (ps2_spad_hit(a)) return *(const ps2_u16a *)PS2_SPAD(a & ~1u);
    return psx_read_half(a);
}
static inline __attribute__((always_inline)) uint32_t ps2_fast_read_word(uint32_t a)
{
    if (__builtin_expect(ps2_kseg0_ram(a), 1)) return ps2_ram_get32(a);
    if (ps2_spad_hit(a)) return *(const ps2_u32a *)PS2_SPAD(a & ~3u);
    return psx_read_word(a);
}
static inline __attribute__((always_inline)) uint8_t ps2_fast_cyc_load_byte(struct CPUState *cpu, uint32_t a,
                                             uint32_t rt, uint32_t reg_mask)
{
    (void)cpu; (void)rt; (void)reg_mask;
    return ps2_fast_read_byte(a);
}
/* V46: LW e LH do código gerado. O psx_cyc.h do psxrecomp chama
 * psx_read_word/psx_read_half, mas ele entra por cpu_state.h ANTES dos
 * #define lá embaixo: todo LW/LH do jogo virava uma chamada a memory.c
 * (o objdump mostra "jal psx_read_word" com endereço fixo de RAM). Mesmo
 * resultado do psx_read_word para todo endereço válido. Com
 * PS2_LOAD_COMPACTO=0 o contador g_prof_scratch_n deixa de contar as
 * leituras do scratchpad em LW/LH (como já era com LB e com as gravações);
 * com o padrão (1) elas continuam passando por memory.c e contando. */
/* PS2_LOAD_COMPACTO=1 (padrão): só a RAM em KSEG0 em linha; scratchpad e o
 * resto pela chamada a memory.c, como antes. No PCSX2 deu o mesmo tempo que
 * tudo em linha (0, igual a ps2_fast_read_*) com 0,9 MB a menos de código
 * (10,3 contra 11,2 MB): no console o cache de instruções (16 KB) pesa. */
#ifndef PS2_LOAD_COMPACTO
#define PS2_LOAD_COMPACTO 1
#endif
static inline __attribute__((always_inline)) uint32_t ps2_fast_cyc_load_word(struct CPUState *cpu, uint32_t a,
                                             uint32_t rt, uint32_t reg_mask)
{
    (void)cpu; (void)rt; (void)reg_mask;
#if PS2_LOAD_COMPACTO
    if (__builtin_expect(ps2_kseg0_ram(a), 1)) return ps2_ram_get32(a);
    return psx_read_word(a);
#else
    return ps2_fast_read_word(a);
#endif
}
static inline __attribute__((always_inline)) uint16_t ps2_fast_cyc_load_half(struct CPUState *cpu, uint32_t a,
                                             uint32_t rt, uint32_t reg_mask)
{
    (void)cpu; (void)rt; (void)reg_mask;
#if PS2_LOAD_COMPACTO
    if (__builtin_expect(ps2_kseg0_ram(a), 1)) return ps2_ram_get16(a);
    return psx_read_half(a);
#else
    return ps2_fast_read_half(a);
#endif
}
static inline __attribute__((always_inline)) void ps2_fast_write_byte(uint32_t a, uint8_t v)
{
    if (__builtin_expect(ps2_kseg0_ram(a), 1)) ps2_ram_set8(a, v);
    else if (ps2_spad_hit(a)) *PS2_SPAD(a) = v;
    else psx_write_byte(a, v);
}
static inline __attribute__((always_inline)) void ps2_fast_write_half(uint32_t a, uint16_t v)
{
    if (__builtin_expect(ps2_kseg0_ram(a), 1)) ps2_ram_set16(a, v);
    else if (ps2_spad_hit(a)) *(ps2_u16a *)PS2_SPAD(a & ~1u) = v;
    else psx_write_half(a, v);
}
static inline __attribute__((always_inline)) void ps2_fast_write_word(uint32_t a, uint32_t v)
{
    if (__builtin_expect(ps2_kseg0_ram(a), 1)) ps2_ram_set32(a, v);
    else if (ps2_spad_hit(a)) *(ps2_u32a *)PS2_SPAD(a & ~3u) = v;
    else psx_write_word(a, v);
}

#ifdef PS2_GENERATED_FAST_MEMORY
/*
 * O código gerado grava "g_debug_last_store_pc = 0x...;" antes de TODA escrita
 * (ferramenta de depuração do psxrecomp). Aqui o nome vira uma variável
 * estática nunca lida: o compilador elimina essas gravações. O "extern" que
 * vem depois em debug_server.h herda a ligação interna (C permite).
 */
#define g_debug_last_store_pc ps2_store_pc_descartado
/* mtc2/mfc2/lwc2 em linha (tools/transformar_gerado.py troca as chamadas) */
#include "cpu_state.h"
#include "gte_regs.h"
/* Ganchos de widescreen do psxrecomp (44 chamadas no código gerado): no
 * port valem o comportamento original 4:3 (iguais a stubs.c), em linha. */
static inline __attribute__((always_inline)) int psx_ws_background_cull_bltz(uint32_t v) { return (int32_t)v < 0; }
static inline __attribute__((always_inline)) int psx_ws_background_cull_margin(void) { return 0; }
static inline __attribute__((always_inline)) void psx_ws_background_packet_tag(uint32_t addr) { (void)addr; }
static inline __attribute__((always_inline)) int psx_ws_cull_sltiu(uint32_t sx, uint32_t imm) { return sx < imm; }
static inline __attribute__((always_inline)) uint32_t psx_ws_full_angle_widen(uint32_t vanilla) { return vanilla; }
static uint32_t ps2_store_pc_descartado __attribute__((unused));
#define psx_read_half      ps2_fast_read_half
#define psx_read_word      ps2_fast_read_word
#define psx_cyc_load_byte  ps2_fast_cyc_load_byte
/* V46: só depois de cpu_state.h (que já trouxe as versões do psx_cyc.h) */
#define psx_cyc_load_word  ps2_fast_cyc_load_word
#define psx_cyc_load_half  ps2_fast_cyc_load_half
/* "cpu->read_word(x)" / "cpu->write_word(x, v)" (e half/byte) do código
 * gerado e de SLUS_004.02_decls.h são trocados por ps2_fast_* em
 * tools/transformar_gerado.py (antes eram macros aqui). */
#endif

/* V56: acesso direto pela janela RAM+scratchpad (depois das ps2_fast_*, que a
 * conferência de janela.h usa como caminho com teste) */
#include "janela.h"

#endif
