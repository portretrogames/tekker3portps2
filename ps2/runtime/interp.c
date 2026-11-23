/*
 * interp.c — interpretador MIPS R3000A (ver interp.h).
 * Referência das instruções: nocash psx-spx (CPU Specifications).
 *
 * Simplificações: o "load delay slot" é ignorado (a carga vale na hora,
 * como no código recompilado); exceções de overflow (ADD/ADDI/SUB) não são
 * geradas. Interrupções são verificadas em fronteiras de instrução fora de
 * delay slot, pelo mesmo ps2_irq_poll do código compilado.
 */
#include "interp.h"
#include "ram.h"
#include "memory.h"
#include "bios_hle.h"
#include "trace_fmv.h"
#include "nativo_overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int psx_game_is_function_entry(uint32_t addr);
extern int psx_syscall(CPUState *cpu, uint32_t code);
extern void gte_execute(CPUState *cpu, uint32_t cmd);
extern uint32_t gte_read_data(CPUState *cpu, uint8_t reg);
extern uint32_t gte_read_ctrl(CPUState *cpu, uint8_t reg);
extern void gte_write_data(CPUState *cpu, uint8_t reg, uint32_t val);
extern void gte_write_ctrl(CPUState *cpu, uint8_t reg, uint32_t val);

unsigned long g_ps2_interp_instr;

static inline uint32_t rd8(uint32_t a)  { return ps2_ram_hit(a) ? ps2_ram_get8(a)  : psx_read_byte(a); }
static inline uint32_t rd16(uint32_t a) { return ps2_ram_hit(a) ? ps2_ram_get16(a) : psx_read_half(a); }
static inline uint32_t rd32(uint32_t a) { return ps2_ram_hit(a) ? ps2_ram_get32(a) : psx_read_word(a); }
static inline void wr8(uint32_t a, uint32_t v)  { if (ps2_ram_hit(a)) ps2_ram_set8(a, (uint8_t)v);   else psx_write_byte(a, (uint8_t)v); }
static inline void wr16(uint32_t a, uint32_t v) { if (ps2_ram_hit(a)) ps2_ram_set16(a, (uint16_t)v); else psx_write_half(a, (uint16_t)v); }
static inline void wr32(uint32_t a, uint32_t v) { if (ps2_ram_hit(a)) ps2_ram_set32(a, v);           else psx_write_word(a, v); }

/* Destino de salto que sai do interpretador? */
extern int psx_game_text_native_ok(uint32_t addr);

static int sai_para(uint32_t alvo)
{
    /* V48: alvos que NÃO são entrada de função compilada (a tabela do
     * despacho é constante, então a resposta não muda). Os laços do overlay
     * faziam uma busca binária em 22 mil funções a cada desvio tomado. */
    static uint32_t nao_entrada[1024];
    unsigned h = (alvo >> 2) & 1023u;
    uint32_t f = alvo & 0x1FFFFFFFu;
    if (f == 0xA0u || f == 0xB0u || f == 0xC0u || alvo == 0x00000F00u)
        return 1;
    if (nao_entrada[h] == alvo && alvo != 0u) return 0;
    /* Só sai se houver código compilado DE VERDADE (não um stub de dado). */
    if (!psx_game_is_function_entry(alvo)) { nao_entrada[h] = alvo; return 0; }
    return psx_game_text_native_ok(alvo);
}

static void erro(CPUState *cpu, uint32_t pc, uint32_t ins, const char *msg)
{
    printf("interp: %s em %08lx (instrucao %08lx)\n", msg,
           (unsigned long)pc, (unsigned long)ins);
    (void)cpu;
    fflush(stdout);
    abort();
}

/*
 * Executa UMA instrução em pc. Devolve 1 se é um salto tomado (alvo em
 * *alvo; o link, se houver, já foi gravado), 0 caso contrário.
 */
static int passo(CPUState *cpu, uint32_t pc, uint32_t *alvo)
{
    uint32_t *r = cpu->gpr;
    uint32_t ins = rd32(pc);
    uint32_t op = ins >> 26, rs = (ins >> 21) & 31u, rt = (ins >> 16) & 31u;
    uint32_t rdn = (ins >> 11) & 31u, sh = (ins >> 6) & 31u, fn = ins & 63u;
    uint32_t imm = ins & 0xFFFFu, simm = (uint32_t)(int32_t)(int16_t)imm;
    uint32_t prox = pc + 4u;
    int salta = 0;

    g_ps2_interp_instr++;
    switch (op) {
    case 0x00:                                   /* SPECIAL */
        switch (fn) {
        case 0x00: r[rdn] = r[rt] << sh; break;                         /* SLL */
        case 0x02: r[rdn] = r[rt] >> sh; break;                         /* SRL */
        case 0x03: r[rdn] = (uint32_t)((int32_t)r[rt] >> sh); break;    /* SRA */
        case 0x04: r[rdn] = r[rt] << (r[rs] & 31u); break;              /* SLLV */
        case 0x06: r[rdn] = r[rt] >> (r[rs] & 31u); break;              /* SRLV */
        case 0x07: r[rdn] = (uint32_t)((int32_t)r[rt] >> (r[rs] & 31u)); break;
        case 0x08: salta = 1; *alvo = r[rs]; break;                     /* JR */
        case 0x09: salta = 1; *alvo = r[rs]; r[rdn] = pc + 8u; break;   /* JALR */
        case 0x0C:                                                      /* SYSCALL */
            cpu->pc = pc;
            psx_syscall(cpu, (ins >> 6) & 0xFFFFFu);
            break;
        case 0x0D: break;                                               /* BREAK */
        case 0x10: r[rdn] = cpu->hi; break;                             /* MFHI */
        case 0x11: cpu->hi = r[rs]; break;
        case 0x12: r[rdn] = cpu->lo; break;                             /* MFLO */
        case 0x13: cpu->lo = r[rs]; break;
        case 0x18: { int64_t m = (int64_t)(int32_t)r[rs] * (int32_t)r[rt];
                     cpu->lo = (uint32_t)m; cpu->hi = (uint32_t)((uint64_t)m >> 32); break; }
        case 0x19: { uint64_t m = (uint64_t)r[rs] * r[rt];
                     cpu->lo = (uint32_t)m; cpu->hi = (uint32_t)(m >> 32); break; }
        case 0x1A: {                                                    /* DIV */
            int32_t n = (int32_t)r[rs], d = (int32_t)r[rt];
            if (d == 0) { cpu->hi = (uint32_t)n; cpu->lo = n >= 0 ? 0xFFFFFFFFu : 1u; }
            else if (n == INT32_MIN && d == -1) { cpu->hi = 0; cpu->lo = (uint32_t)n; }
            else { cpu->lo = (uint32_t)(n / d); cpu->hi = (uint32_t)(n % d); }
            break; }
        case 0x1B: {                                                    /* DIVU */
            uint32_t n = r[rs], d = r[rt];
            if (d == 0) { cpu->hi = n; cpu->lo = 0xFFFFFFFFu; }
            else { cpu->lo = n / d; cpu->hi = n % d; }
            break; }
        case 0x20: case 0x21: r[rdn] = r[rs] + r[rt]; break;            /* ADD(U) */
        case 0x22: case 0x23: r[rdn] = r[rs] - r[rt]; break;            /* SUB(U) */
        case 0x24: r[rdn] = r[rs] & r[rt]; break;
        case 0x25: r[rdn] = r[rs] | r[rt]; break;
        case 0x26: r[rdn] = r[rs] ^ r[rt]; break;
        case 0x27: r[rdn] = ~(r[rs] | r[rt]); break;                    /* NOR */
        case 0x2A: r[rdn] = (int32_t)r[rs] < (int32_t)r[rt]; break;     /* SLT */
        case 0x2B: r[rdn] = r[rs] < r[rt]; break;                       /* SLTU */
        default: erro(cpu, pc, ins, "SPECIAL desconhecida");
        }
        break;
    case 0x01: {                                 /* BLTZ/BGEZ/BLTZAL/BGEZAL */
        int cond = (rt & 1u) ? (int32_t)r[rs] >= 0 : (int32_t)r[rs] < 0;
        if ((rt & 0x1Eu) == 0x10u) r[31] = pc + 8u;
        if (cond) { salta = 1; *alvo = prox + (simm << 2); }
        break; }
    case 0x02: salta = 1; *alvo = (prox & 0xF0000000u) | ((ins & 0x3FFFFFFu) << 2); break;  /* J */
    case 0x03: salta = 1; *alvo = (prox & 0xF0000000u) | ((ins & 0x3FFFFFFu) << 2);         /* JAL */
               r[31] = pc + 8u; break;
    case 0x04: if (r[rs] == r[rt]) { salta = 1; *alvo = prox + (simm << 2); } break;       /* BEQ */
    case 0x05: if (r[rs] != r[rt]) { salta = 1; *alvo = prox + (simm << 2); } break;       /* BNE */
    case 0x06: if ((int32_t)r[rs] <= 0) { salta = 1; *alvo = prox + (simm << 2); } break;  /* BLEZ */
    case 0x07: if ((int32_t)r[rs] > 0) { salta = 1; *alvo = prox + (simm << 2); } break;   /* BGTZ */
    case 0x08: case 0x09: r[rt] = r[rs] + simm; break;                 /* ADDI(U) */
    case 0x0A: r[rt] = (int32_t)r[rs] < (int32_t)simm; break;           /* SLTI */
    case 0x0B: r[rt] = r[rs] < simm; break;                              /* SLTIU */
    case 0x0C: r[rt] = r[rs] & imm; break;
    case 0x0D: r[rt] = r[rs] | imm; break;
    case 0x0E: r[rt] = r[rs] ^ imm; break;
    case 0x0F: r[rt] = imm << 16; break;                                 /* LUI */
    case 0x10:                                                           /* COP0 */
        if (rs == 0x00) r[rt] = cpu->cop0[rdn];                          /* MFC0 */
        else if (rs == 0x04) cpu->cop0[rdn] = r[rt];                     /* MTC0 */
        else if (rs == 0x10 && fn == 0x10) {                             /* RFE */
            uint32_t sr = cpu->cop0[12];
            cpu->cop0[12] = (sr & ~0x0Fu) | ((sr >> 2) & 0x0Fu);
        }
        break;
    case 0x12:                                                           /* COP2 (GTE) */
        if (rs & 0x10u) gte_execute(cpu, ins & 0x1FFFFFFu);
        else if (rs == 0x00) r[rt] = gte_read_data(cpu, (uint8_t)rdn);   /* MFC2 */
        else if (rs == 0x02) r[rt] = gte_read_ctrl(cpu, (uint8_t)rdn);   /* CFC2 */
        else if (rs == 0x04) gte_write_data(cpu, (uint8_t)rdn, r[rt]);   /* MTC2 */
        else if (rs == 0x06) gte_write_ctrl(cpu, (uint8_t)rdn, r[rt]);   /* CTC2 */
        break;
    case 0x20: r[rt] = (uint32_t)(int32_t)(int8_t)rd8(r[rs] + simm); break;   /* LB */
    case 0x21: r[rt] = (uint32_t)(int32_t)(int16_t)rd16(r[rs] + simm); break; /* LH */
    case 0x22: {                                                         /* LWL */
        uint32_t a = r[rs] + simm, w = rd32(a & ~3u), s = (a & 3u) * 8u;
        r[rt] = (r[rt] & (0x00FFFFFFu >> s)) | (w << (24u - s)); break; }
    case 0x23: r[rt] = rd32(r[rs] + simm); break;                       /* LW */
    case 0x24: r[rt] = rd8(r[rs] + simm); break;                        /* LBU */
    case 0x25: r[rt] = rd16(r[rs] + simm); break;                       /* LHU */
    case 0x26: {                                                         /* LWR */
        uint32_t a = r[rs] + simm, w = rd32(a & ~3u), s = (a & 3u) * 8u;
        r[rt] = (s == 0u) ? w : ((r[rt] & (0xFFFFFFFFu << (32u - s))) | (w >> s)); break; }
    case 0x28: wr8(r[rs] + simm, r[rt]); break;                         /* SB */
    case 0x29: wr16(r[rs] + simm, r[rt]); break;                        /* SH */
    case 0x2A: {                                                         /* SWL */
        uint32_t a = r[rs] + simm, al = a & ~3u, w = rd32(al), s = (a & 3u) * 8u;
        uint32_t m = (s == 24u) ? 0u : (0xFFFFFFFFu << (s + 8u));
        wr32(al, (w & m) | (r[rt] >> (24u - s))); break; }
    case 0x2B: wr32(r[rs] + simm, r[rt]); break;                        /* SW */
    case 0x2E: {                                                         /* SWR */
        uint32_t a = r[rs] + simm, al = a & ~3u, w = rd32(al), s = (a & 3u) * 8u;
        uint32_t m = (s == 0u) ? 0u : (0xFFFFFFFFu >> (32u - s));
        wr32(al, (w & m) | (r[rt] << s)); break; }
    case 0x32: gte_write_data(cpu, (uint8_t)rt, rd32(r[rs] + simm)); break;   /* LWC2 */
    case 0x3A: wr32(r[rs] + simm, gte_read_data(cpu, (uint8_t)rt)); break;    /* SWC2 */
    default: erro(cpu, pc, ins, "instrucao desconhecida");
    }
    r[0] = 0;
    return salta;
}

/*
 * V48: rotinas de overlay traduzidas para C (tools/nativo.py com
 * tools/nativas_overlay.txt, em build/r/nativo.c). Antes de usar a versão
 * nativa confere se a RAM tem exatamente o código que foi traduzido: outro
 * overlay pode estar no mesmo endereço (aí segue interpretando).
 */
static int nativa_overlay(CPUState *cpu, uint32_t pc)
{
    unsigned i;
    for (i = 0; i < ps2_nat_n_overlays; i++) {
        const Ps2NatOverlay *o = &ps2_nat_overlays[i];
        uint32_t f = pc & 0x1FFFFFFFu;
        if (pc != o->ini || f + o->n * 4u > 0x200000u) continue;
        /* bytes da RAM do PS1 = palavras little-endian, como no EE */
        if (memcmp(&g_ps1_ram[f], o->cod, o->n * 4u) != 0) continue;
        cpu->pc = pc;
        return ps2_nat_executar_overlay(cpu, pc, o->nat);   /* cpu->pc = retorno */
    }
    return 0;
}

/* Verificação das rotinas nativas (NAT_VERIFICAR=1): roda o original
 * interpretado de pc até voltar a ret, sem atender interrupções. */
unsigned ps2_interp_ate(CPUState *cpu, uint32_t pc, uint32_t ret)
{
    unsigned n = 0;
    while (pc != ret && n < 50000000u) {
        uint32_t alvo, ignorado;
        n++;
        if (passo(cpu, pc, &alvo)) {
            (void)passo(cpu, pc + 4u, &ignorado);
            pc = alvo;
        } else pc += 4u;
    }
    cpu->pc = pc;
    return n;
}

int ps2_interp_run(CPUState *cpu)
{
    static unsigned contagem;
    uint32_t pc = cpu->pc;

    if (!ps2_ram_hit(pc))
        return 0;
    if (nativa_overlay(cpu, pc)) {
        pc = cpu->pc;
        if (sai_para(pc) || !ps2_ram_hit(pc)) return 1;
    }
    for (;;) {
        uint32_t alvo, ignorado;
        if (passo(cpu, pc, &alvo)) {
            (void)passo(cpu, pc + 4u, &ignorado);        /* delay slot */
            pc = alvo;
            if (sai_para(pc)) { cpu->pc = pc; return 1; }
            if (nativa_overlay(cpu, pc)) {
                pc = cpu->pc;
                if (sai_para(pc) || !ps2_ram_hit(pc)) return 1;
            }
        } else {
            pc += 4u;
        }
        if ((++contagem & 255u) == 0u) {
#ifdef PS2_TRACE_FMV
            if (g_tr_ativo) ps2_tr_pci(pc);
#endif
            cpu->pc = pc;
            ps2_irq_poll(cpu, pc);                       /* pode fazer longjmp */
        }
    }
}
