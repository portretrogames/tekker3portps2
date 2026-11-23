/*
 * Acesso aos registradores do GTE. Referências: PSXRecomp (mstan,
 * PolyForm Noncommercial 1.0.0) e documentação PSX-SPX.
 * Os comandos ainda permanecem em stubs.c.
 */
#include <stdint.h>
#include "cpu_state.h"

#include "gte_regs.h"

uint32_t gte_read_data(CPUState *cpu, uint8_t reg) { return gte_rd_data_i(cpu, reg); }

uint32_t gte_read_ctrl(CPUState *cpu, uint8_t reg)
{
    reg &= 31u;
    switch (reg) {
    case 4: case 12: case 20: case 26: case 27: case 29: case 30:
        return sinal16(cpu->gte_ctrl[reg]);
    default: return cpu->gte_ctrl[reg];
    }
}

void gte_write_data(CPUState *cpu, uint8_t reg, uint32_t valor) { gte_wr_data_i(cpu, reg, valor); }

void gte_write_ctrl(CPUState *cpu, uint8_t reg, uint32_t valor)
{
    reg &= 31u;
    switch (reg) {
    case 4: case 12: case 20: case 26: case 27: case 29: case 30:
        cpu->gte_ctrl[reg] = sinal16(valor);
        break;
    case 31:
        valor &= 0x7ffff000u;
        cpu->gte_ctrl[31] = valor | ((valor & 0x7f87e000u) ? 0x80000000u : 0);
        break;
    default:
        cpu->gte_ctrl[reg] = valor;
        break;
    }
}

/* Obs.: valores com sinal são escalados com multiplicação (x 4096), não com
 * "<<", porque deslocar negativo para a esquerda é comportamento indefinido
 * em C (achado F7 da revisão técnica).
 * ======================================================================
 * Comandos do GTE (gte_execute). Referência: nocash psx-spx
 * ("Geometry Transformation Engine"). Aritmética em 64 bits com as
 * verificações de overflow de 44 bits (MAC1-3), 32 bits (MAC0) e as
 * saturações/flags do hardware.
 * ====================================================================== */

#define D(n)  (cpu->gte_data[n])
#define C(n)  (cpu->gte_ctrl[n])

static uint32_t FLAG;

static inline __attribute__((always_inline)) int32_t lo16(uint32_t v) { return (int16_t)(v & 0xFFFFu); }
/* 32 x 32 -> 64 bits: no R5900 vira um "mult". Multiplicar dois int64_t
 * chama __muldi3 (o EE não tem multiplicação de 64 bits): V22 troca todas. */
#ifdef _EE
static inline int64_t mul32(int32_t a, int32_t b)
{
    /* V29: PMFHL.LW junta LO e HI num registrador de 64 bits (1 instrução) */
    int64_t r;
    __asm__("mult %1, %2\n\tpmfhl.lw %0" : "=r"(r) : "r"(a), "r"(b) : "hi", "lo");
    return r;
}
/* a1*b1 + a2*b2 + a3*b3 em 64 bits: MULT + 2 MADD acumulam em HI:LO */
static inline int64_t prod3(int32_t a1, int32_t b1, int32_t a2, int32_t b2, int32_t a3, int32_t b3)
{
    int64_t r;
    __asm__("mult %1, %2\n\tmadd %3, %4\n\tmadd %5, %6\n\tpmfhl.lw %0"
            : "=r"(r) : "r"(a1), "r"(b1), "r"(a2), "r"(b2), "r"(a3), "r"(b3) : "hi", "lo");
    return r;
}
/* V60: dois prod3 ao mesmo tempo. O EE tem dois multiplicadores: o 0 (MULT/MADD,
 * HI/LO) e o 1 (MULT1/MADD1, HI1/LO1); um não espera o outro. PMFHL.LW junta
 * HI:LO dos dois num registrador de 128 bits (64 de baixo = multiplicador 0,
 * 64 de cima = 1); PCPYUD traz a metade de cima. O GCC não conhece HI1/LO1 (não
 * há clobber para eles): o Makefile (build/mult1_ok) para o build se o
 * multiplicador 1 aparecer no ELF fora de gte.o. As saídas não são early-clobber:
 * PMFHL/PCPYUD vêm depois da última leitura das entradas. */
static inline __attribute__((always_inline)) void prod3_par(
    int32_t a1, int32_t b1, int32_t a2, int32_t b2, int32_t a3, int32_t b3,
    int32_t c1, int32_t d1, int32_t c2, int32_t d2, int32_t c3, int32_t d3, int64_t *r0, int64_t *r1)
{
    int64_t p, q;
    __asm__("mult %2, %3\n\tmult1 %8, %9\n\tmadd %4, %5\n\tmadd1 %10, %11\n\t"
            "madd %6, %7\n\tmadd1 %12, %13\n\tpmfhl.lw %0\n\tpcpyud %1, %0, %0"
            : "=r"(p), "=r"(q)
            : "r"(a1), "r"(b1), "r"(a2), "r"(b2), "r"(a3), "r"(b3),
              "r"(c1), "r"(d1), "r"(c2), "r"(d2), "r"(c3), "r"(d3) : "hi", "lo");
    *r0 = p; *r1 = q;
}
static inline __attribute__((always_inline)) void mul32_par(int32_t a, int32_t b, int32_t c, int32_t d,
                                                            int64_t *r0, int64_t *r1)
{
    int64_t p, q;
    __asm__("mult %2, %3\n\tmult1 %4, %5\n\tpmfhl.lw %0\n\tpcpyud %1, %0, %0"
            : "=r"(p), "=r"(q) : "r"(a), "r"(b), "r"(c), "r"(d) : "hi", "lo");
    *r0 = p; *r1 = q;
}
#else
static inline int64_t mul32(int32_t a, int32_t b) { return (int64_t)a * (int64_t)b; }
static inline int64_t prod3(int32_t a1, int32_t b1, int32_t a2, int32_t b2, int32_t a3, int32_t b3)
{ return (int64_t)a1 * b1 + (int64_t)a2 * b2 + (int64_t)a3 * b3; }
static inline void prod3_par(int32_t a1, int32_t b1, int32_t a2, int32_t b2, int32_t a3, int32_t b3,
                             int32_t c1, int32_t d1, int32_t c2, int32_t d2, int32_t c3, int32_t d3,
                             int64_t *r0, int64_t *r1)
{ *r0 = prod3(a1, b1, a2, b2, a3, b3); *r1 = prod3(c1, d1, c2, d2, c3, d3); }
static inline void mul32_par(int32_t a, int32_t b, int32_t c, int32_t d, int64_t *r0, int64_t *r1)
{ *r0 = mul32(a, b); *r1 = mul32(c, d); }
#endif
/* V29: caminho rápido. Fatores de 16 bits dão produtos < 2^30; com o vetor
 * de translação |t| < 2^30, t*4096 + 3 produtos < 2^43: nenhuma soma parcial
 * estoura os 44 bits do MAC, então as checagens A() não mudam nada e saem. */
int g_gte_rapido = 1;
long g_gte_autoteste_dif = -1;   /* V60: -1 = autoteste do boot não rodou */
static inline __attribute__((always_inline)) int t_pequeno(uint32_t a, uint32_t b, uint32_t c)
{
    return ((a + 0x40000000u) | (b + 0x40000000u) | (c + 0x40000000u)) < 0x80000000u;
}
static inline __attribute__((always_inline)) int32_t hi16(uint32_t v) { return (int16_t)(v >> 16); }

/* Matriz 3x3 compactada em 5 registradores (RT: 0, L: 8, LR: 16). */
static inline __attribute__((always_inline)) int32_t M(CPUState *cpu, unsigned base, unsigned l, unsigned c)
{
    unsigned i = l * 3u + c;              /* 0..8 */
    uint32_t r = C(base + i / 2u);
    return (i & 1u) ? hi16(r) : lo16(r);
}

/* Overflow de 44 bits no MAC1..3 e extensão de sinal para 44 bits. */
/* V24: caminho rápido com uma só comparação (quase nunca há overflow); a
 * extensão de sinal só muda o valor quando há overflow. Mesmo resultado. */
static inline int64_t A(int i, int64_t v)
{
    if (__builtin_expect((uint64_t)(v + 0x80000000000LL) >= 0x100000000000ULL, 0)) {
        if (v > 0x7FFFFFFFFFFLL) FLAG |= 1u << (31 - i);   /* 30,29,28 */
        else                     FLAG |= 1u << (28 - i);   /* 27,26,25 */
        return (int64_t)((uint64_t)v << 20) >> 20;
    }
    return v;
}

static inline __attribute__((always_inline)) void set_mac(CPUState *cpu, int i, int64_t v, int sh)
{
    D(24 + i) = (uint32_t)(int32_t)(A(i, v) >> sh);
}

static inline __attribute__((always_inline)) int32_t lim_ir(int i, int32_t v, int lm)
{
    int32_t min = lm ? 0 : -0x8000;
    if (v < min)     { FLAG |= 1u << (25 - i); return min; }    /* 24,23,22 */
    if (v > 0x7FFF)  { FLAG |= 1u << (25 - i); return 0x7FFF; }
    return v;
}

static inline __attribute__((always_inline)) void set_ir(CPUState *cpu, int i, int32_t v, int lm)
{
    D(8 + i) = (uint32_t)lim_ir(i, v, lm);
}

static inline __attribute__((always_inline)) void set_mac_ir(CPUState *cpu, int i, int64_t v, int sh, int lm)
{
    set_mac(cpu, i, v, sh);
    set_ir(cpu, i, (int32_t)D(24 + i), lm);
}

static inline int64_t F(int64_t v)           /* MAC0: 32 bits */
{
    if (__builtin_expect((uint64_t)(v + 0x80000000LL) >= 0x100000000ULL, 0)) {
        if (v > 0x7FFFFFFFLL) FLAG |= 1u << 16;
        else                  FLAG |= 1u << 15;
    }
    return v;
}

static inline __attribute__((always_inline)) void set_ir0(CPUState *cpu, int32_t v)
{
    if (v < 0)      { FLAG |= 1u << 12; v = 0; }
    if (v > 0x1000) { FLAG |= 1u << 12; v = 0x1000; }
    D(8) = (uint32_t)v;
}

static inline __attribute__((always_inline)) void push_sz(CPUState *cpu, int32_t z)
{
    if (z < 0)      { FLAG |= 1u << 18; z = 0; }
    if (z > 0xFFFF) { FLAG |= 1u << 18; z = 0xFFFF; }
    D(16) = D(17); D(17) = D(18); D(18) = D(19); D(19) = (uint32_t)z;
}

static inline __attribute__((always_inline)) void push_sxy(CPUState *cpu, int32_t x, int32_t y)
{
    if (x < -0x400) { FLAG |= 1u << 14; x = -0x400; }
    if (x > 0x3FF)  { FLAG |= 1u << 14; x = 0x3FF; }
    if (y < -0x400) { FLAG |= 1u << 13; y = -0x400; }
    if (y > 0x3FF)  { FLAG |= 1u << 13; y = 0x3FF; }
    D(12) = D(13); D(13) = D(14);
    D(14) = D(15) = ((uint32_t)y << 16) | ((uint32_t)x & 0xFFFFu);
}

static inline __attribute__((always_inline)) uint32_t cor8(int bit, int32_t v)
{
    if (v < 0)   { FLAG |= 1u << bit; return 0; }
    if (v > 255) { FLAG |= 1u << bit; return 255; }
    return (uint32_t)v;
}

static inline __attribute__((always_inline)) void push_cor(CPUState *cpu)
{
    uint32_t r = cor8(21, (int32_t)D(25) >> 4);
    uint32_t g = cor8(20, (int32_t)D(26) >> 4);
    uint32_t b = cor8(19, (int32_t)D(27) >> 4);
    D(20) = D(21); D(21) = D(22);
    D(22) = r | (g << 8) | (b << 16) | (D(6) & 0xFF000000u);
}

/* ---- divisão UNR (H / SZ3) ---- */
static uint8_t unr[0x101];
static int unr_pronta;

static void unr_iniciar(void)
{
    int i;
    for (i = 0; i <= 0x100; i++) {
        int v = (0x40000 / (i + 0x100) + 1) / 2 - 0x101;
        unr[i] = (uint8_t)(v < 0 ? 0 : v);
    }
    unr_pronta = 1;
}

static inline __attribute__((always_inline)) uint32_t dividir(uint32_t h, uint32_t sz3)
{
    uint32_t z, n, d, u;
    if (h >= sz3 * 2u) { FLAG |= 1u << 17; return 0x1FFFFu; }
    if (!unr_pronta) unr_iniciar();
    /* aqui sz3 != 0 (sz3 == 0 sai acima), então há um bit 1 nos 16 de baixo */
    {   /* zeros à esquerda em 16 bits (o EE não tem clz: __builtin_clz vira __clzdi2) */
        uint32_t v = sz3 & 0xFFFFu;
        z = 0;
        if (!(v & 0xFF00u)) { z += 8; v <<= 8; }
        if (!(v & 0xF000u)) { z += 4; v <<= 4; }
        if (!(v & 0xC000u)) { z += 2; v <<= 2; }
        if (!(v & 0x8000u)) { z += 1; }
    }
    n = h << z;
    d = sz3 << z;
    u = (uint32_t)unr[(d - 0x7FC0u) >> 7] + 0x101u;
    d = (0x2000080u - (d * u)) >> 8;
    d = (0x0000080u + (d * u)) >> 8;
    n = (uint32_t)(((uint64_t)n * d + 0x8000u) >> 16);
    return n > 0x1FFFFu ? 0x1FFFFu : n;
}

/* ---- RTPS/RTPT ---- */
static inline __attribute__((always_inline)) int64_t rtp(CPUState *cpu, int v, int sh, int lm, int ultimo)
{
    int32_t vx = lo16(D(v * 2)), vy = hi16(D(v * 2)), vz = lo16(D(v * 2 + 1));
    int64_t x, y, z;
    int64_t n, m0;
    int32_t z12;
    /* V45: só o caminho completo; o rápido (TR pequeno) é rtp_rapido() */
    x = A(1, A(1, A(1, ((int64_t)(int32_t)C(5) * 4096) + mul32(M(cpu, 0, 0, 0), vx))
                    + mul32(M(cpu, 0, 0, 1), vy)) + mul32(M(cpu, 0, 0, 2), vz));
    y = A(2, A(2, A(2, ((int64_t)(int32_t)C(6) * 4096) + mul32(M(cpu, 0, 1, 0), vx))
                    + mul32(M(cpu, 0, 1, 1), vy)) + mul32(M(cpu, 0, 1, 2), vz));
    z = A(3, A(3, A(3, ((int64_t)(int32_t)C(7) * 4096) + mul32(M(cpu, 0, 2, 0), vx))
                    + mul32(M(cpu, 0, 2, 1), vy)) + mul32(M(cpu, 0, 2, 2), vz));

    D(25) = (uint32_t)(int32_t)(x >> sh);
    D(26) = (uint32_t)(int32_t)(y >> sh);
    D(27) = (uint32_t)(int32_t)(z >> sh);
    set_ir(cpu, 1, (int32_t)D(25), lm);
    set_ir(cpu, 2, (int32_t)D(26), lm);
    /* IR3: valor saturado normal, mas a flag usa MAC3 >> 12 (quirk) */
    {
        int32_t m3 = (int32_t)D(27), min = lm ? 0 : -0x8000;
        D(11) = (uint32_t)(m3 < min ? min : m3 > 0x7FFF ? 0x7FFF : m3);
        z12 = (int32_t)(z >> 12);
        if (z12 < -0x8000 || z12 > 0x7FFF) FLAG |= 1u << 22;
    }
    push_sz(cpu, (int32_t)(z >> 12));
    n = dividir(C(26) & 0xFFFFu, D(19) & 0xFFFFu);
    m0 = F(mul32((int32_t)n, (int32_t)D(9)) + (int32_t)C(24));
    {
        int32_t sx = (int32_t)(m0 >> 16);
        m0 = F(mul32((int32_t)n, (int32_t)D(10)) + (int32_t)C(25));
        push_sxy(cpu, sx, (int32_t)(m0 >> 16));
    }
    if (ultimo) {
        m0 = F(mul32((int32_t)n, lo16(C(27))) + (int32_t)C(28));
        D(24) = (uint32_t)(int32_t)m0;
        set_ir0(cpu, (int32_t)(m0 >> 12));
    }
    return n;
}

/* V45: RTPS/RTPT fora do caminho rápido (TR grande ou g_gte_rapido = 0): raro,
 * então fica fora de linha para não inchar cada gte_k_* (cache de instruções). */
static __attribute__((noinline, cold)) void rtp_lento(CPUState *cpu, int tres, int sh, int lm)
{
    if (tres) { rtp(cpu, 0, sh, lm, 0); rtp(cpu, 1, sh, lm, 0); rtp(cpu, 2, sh, lm, 1); }
    else rtp(cpu, 0, sh, lm, 1);
}

/* ---- V45: RTPS/RTPT rápidos ----
 * Mesmo resultado do rtp() no caminho rápido (TR pequeno: os MAC não estouram
 * os 44 bits), mas: a matriz, o TR e as constantes são lidos uma vez; FLAG fica
 * num registro; MAC/IR dos vértices 0 e 1 (que o 3º sobrescreve) e as filas
 * SZ/SXY são gravados só no fim; os zeros à esquerda da divisão saem numa
 * instrução (PLZCW no EE). Conferido no boot contra o caminho completo
 * (gte_bench) e no PC por tests/gte_test.c. */
static inline __attribute__((always_inline)) uint32_t clz16_nz(uint32_t v)   /* v em 1..0xFFFF */
{
#ifdef _EE
    uint64_t r;
    /* PLZCW: bits iguais ao bit de sinal no início de cada palavra, menos 1.
     * Palavra positiva: zeros à esquerda (de 32) - 1 = (16 + z16) - 1. */
    __asm__("plzcw %0, %1" : "=r"(r) : "r"((uint64_t)v));
    return (uint32_t)r - 15u;
#else
    return (uint32_t)__builtin_clz(v) - 16u;
#endif
}

static inline __attribute__((always_inline)) uint32_t dividir_r(uint32_t h, uint32_t sz3, uint32_t *fl)
{   /* = dividir(), mas com FLAG local e PLZCW. A duplicação é de propósito: o
     * caminho completo (dividir) fica como referência independente, e o
     * autoteste do boot compara os dois (confere o PLZCW no console).
     * A tabela unr já tem que estar pronta (rtp_rapido garante). */
    uint32_t z, n, d, u;
    if (h >= sz3 * 2u) { *fl |= 1u << 17; return 0x1FFFFu; }
    z = clz16_nz(sz3 & 0xFFFFu);
    n = h << z;
    d = sz3 << z;
    u = (uint32_t)unr[(d - 0x7FC0u) >> 7] + 0x101u;
    d = (0x2000080u - (d * u)) >> 8;
    d = (0x0000080u + (d * u)) >> 8;
    n = (uint32_t)(((uint64_t)n * d + 0x8000u) >> 16);
    return n > 0x1FFFFu ? 0x1FFFFu : n;
}

static inline __attribute__((always_inline)) void f_r(int64_t v, uint32_t *fl)   /* = F() */
{
    if (__builtin_expect((uint64_t)v + 0x80000000ULL >= 0x100000000ULL, 0))
        *fl |= v > 0x7FFFFFFFLL ? 1u << 16 : 1u << 15;
}

static inline __attribute__((always_inline)) void rtp_rapido(CPUState *cpu, const int nv, int sh, int lm)
{   /* nv = 1 (RTPS, vértice 0) ou 3 (RTPT); só com t_pequeno(TR) e g_gte_rapido */
    const int32_t r11 = lo16(C(0)), r12 = hi16(C(0)), r13 = lo16(C(1));
    const int32_t r21 = hi16(C(1)), r22 = lo16(C(2)), r23 = hi16(C(2));
    const int32_t r31 = lo16(C(3)), r32 = hi16(C(3)), r33 = lo16(C(4));
    const int64_t tx = (int64_t)(int32_t)C(5) * 4096, ty = (int64_t)(int32_t)C(6) * 4096,
                  tz = (int64_t)(int32_t)C(7) * 4096;
    const int32_t ofx = (int32_t)C(24), ofy = (int32_t)C(25);
    const uint32_t h = C(26) & 0xFFFFu;
    const int32_t irmin = lm ? 0 : -0x8000;
    uint32_t fl = 0, n = 0, zf[3], sxyf[3];
    int32_t mac1 = 0, mac2 = 0, mac3 = 0, ir1 = 0, ir2 = 0, ir3 = 0;
    int v;
    if (__builtin_expect(!unr_pronta, 0)) unr_iniciar();
    #pragma GCC unroll 3
    for (v = 0; v < nv; v++) {
        const int32_t vx = lo16(D(v * 2)), vy = hi16(D(v * 2)), vz = lo16(D(v * 2 + 1));
        int64_t px, py;
        prod3_par(r11, vx, r12, vy, r13, vz, r21, vx, r22, vy, r23, vz, &px, &py);   /* V60 */
        const int64_t x = tx + px, y = ty + py;
        const int64_t z = tz + prod3(r31, vx, r32, vy, r33, vz);
        int32_t z12 = (int32_t)(z >> 12), sz, sx, sy;
        int64_t m0;
        mac1 = (int32_t)(x >> sh); mac2 = (int32_t)(y >> sh); mac3 = (int32_t)(z >> sh);
        ir1 = mac1;
        if (ir1 < irmin)       { fl |= 1u << 24; ir1 = irmin; }
        else if (ir1 > 0x7FFF) { fl |= 1u << 24; ir1 = 0x7FFF; }
        ir2 = mac2;
        if (ir2 < irmin)       { fl |= 1u << 23; ir2 = irmin; }
        else if (ir2 > 0x7FFF) { fl |= 1u << 23; ir2 = 0x7FFF; }
        ir3 = mac3 < irmin ? irmin : mac3 > 0x7FFF ? 0x7FFF : mac3;   /* quirk: flag pelo z >> 12 */
        if (z12 < -0x8000 || z12 > 0x7FFF) fl |= 1u << 22;
        sz = z12;                                                     /* push_sz */
        if (sz < 0)           { fl |= 1u << 18; sz = 0; }
        else if (sz > 0xFFFF) { fl |= 1u << 18; sz = 0xFFFF; }
        zf[v] = (uint32_t)sz;
        n = dividir_r(h, (uint32_t)sz, &fl);
        {
            int64_t m1;
            mul32_par((int32_t)n, ir1, (int32_t)n, ir2, &m0, &m1);              /* V60 */
            m0 += ofx; f_r(m0, &fl); sx = (int32_t)(m0 >> 16);
            m1 += ofy; f_r(m1, &fl); sy = (int32_t)(m1 >> 16);
        }
        if (sx < -0x400)     { fl |= 1u << 14; sx = -0x400; }       /* push_sxy */
        else if (sx > 0x3FF) { fl |= 1u << 14; sx = 0x3FF; }
        if (sy < -0x400)     { fl |= 1u << 13; sy = -0x400; }
        else if (sy > 0x3FF) { fl |= 1u << 13; sy = 0x3FF; }
        sxyf[v] = ((uint32_t)sy << 16) | ((uint32_t)sx & 0xFFFFu);
    }
    {   /* último vértice: profundidade (MAC0, IR0) */
        int64_t m0 = mul32((int32_t)n, lo16(C(27))) + (int32_t)C(28);
        int32_t i0;
        f_r(m0, &fl);
        D(24) = (uint32_t)(int32_t)m0;
        i0 = (int32_t)(m0 >> 12);
        if (i0 < 0)           { fl |= 1u << 12; i0 = 0; }
        else if (i0 > 0x1000) { fl |= 1u << 12; i0 = 0x1000; }
        D(8) = (uint32_t)i0;
    }
    D(25) = (uint32_t)mac1; D(26) = (uint32_t)mac2; D(27) = (uint32_t)mac3;
    D(9) = (uint32_t)ir1; D(10) = (uint32_t)ir2; D(11) = (uint32_t)ir3;
    if (nv == 3) {
        D(16) = D(19); D(17) = zf[0]; D(18) = zf[1]; D(19) = zf[2];
        D(12) = sxyf[0]; D(13) = sxyf[1]; D(14) = D(15) = sxyf[2];
    } else {
        D(16) = D(17); D(17) = D(18); D(18) = D(19); D(19) = zf[0];
        D(12) = D(13); D(13) = D(14); D(14) = D(15) = sxyf[0];
    }
    FLAG |= fl;
}

/* ---- iluminação e cor ---- */

/* MAC = (matriz * vetor [+ (cv * 4096)]) >> sh; IR saturado */
static inline __attribute__((always_inline)) void mat_vec(CPUState *cpu, unsigned mbase, int32_t vx, int32_t vy, int32_t vz,
                    int64_t t1, int64_t t2, int64_t t3, int sh, int lm)
{
    int64_t r[3]; int l;
    int64_t t[3] = { t1, t2, t3 };
    if (__builtin_expect(g_gte_rapido && t_pequeno((uint32_t)t1, (uint32_t)t2, (uint32_t)t3), 1)) {
        #pragma GCC unroll 3
        for (l = 0; l < 3; l++)
            r[l] = (t[l] * 4096) + prod3(M(cpu, mbase, (unsigned)l, 0), vx, M(cpu, mbase, (unsigned)l, 1), vy,
                                        M(cpu, mbase, (unsigned)l, 2), vz);
    } else
    #pragma GCC unroll 3
    for (l = 0; l < 3; l++)
        r[l] = A(l + 1, A(l + 1, A(l + 1, (t[l] * 4096) + (int64_t)M(cpu, mbase, (unsigned)l, 0) * vx)
                 + (int64_t)M(cpu, mbase, (unsigned)l, 1) * vy) + (int64_t)M(cpu, mbase, (unsigned)l, 2) * vz);
    #pragma GCC unroll 3
    for (l = 0; l < 3; l++)
        set_mac_ir(cpu, l + 1, r[l], sh, lm);
}

static inline __attribute__((always_inline)) void luz(CPUState *cpu, int v, int sh, int lm)        /* L * V */
{
    mat_vec(cpu, 8, lo16(D(v * 2)), hi16(D(v * 2)), lo16(D(v * 2 + 1)), 0, 0, 0, sh, lm);
}

static inline __attribute__((always_inline)) void cor_luz(CPUState *cpu, int sh, int lm)           /* BK + LR * IR */
{
    mat_vec(cpu, 16, (int32_t)D(9), (int32_t)D(10), (int32_t)D(11),
            (int32_t)C(13), (int32_t)C(14), (int32_t)C(15), sh, lm);
}

/* MAC+(FC-MAC)*IR0, a partir de MAC "cru" (sem shift) */
static inline __attribute__((always_inline)) void interpolar(CPUState *cpu, int64_t m1, int64_t m2, int64_t m3, int sh, int lm)
{
    set_mac_ir(cpu, 1, ((int64_t)(int32_t)C(21) * 4096) - m1, sh, 0);
    set_mac_ir(cpu, 2, ((int64_t)(int32_t)C(22) * 4096) - m2, sh, 0);
    set_mac_ir(cpu, 3, ((int64_t)(int32_t)C(23) * 4096) - m3, sh, 0);
    set_mac_ir(cpu, 1, mul32((int32_t)D(9),  (int32_t)D(8)) + m1, sh, lm);
    set_mac_ir(cpu, 2, mul32((int32_t)D(10), (int32_t)D(8)) + m2, sh, lm);
    set_mac_ir(cpu, 3, mul32((int32_t)D(11), (int32_t)D(8)) + m3, sh, lm);
}

static inline __attribute__((always_inline)) void cor_vezes_ir(CPUState *cpu, int sh, int lm, int interp)   /* RGBC * IR */
{
    int64_t m1 = mul32((int32_t)((D(6) & 0xFFu) << 4), (int32_t)D(9));
    int64_t m2 = mul32((int32_t)(((D(6) >> 8) & 0xFFu) << 4), (int32_t)D(10));
    int64_t m3 = mul32((int32_t)(((D(6) >> 16) & 0xFFu) << 4), (int32_t)D(11));
    if (interp) interpolar(cpu, m1, m2, m3, sh, lm);
    else {
        set_mac_ir(cpu, 1, m1, sh, lm);
        set_mac_ir(cpu, 2, m2, sh, lm);
        set_mac_ir(cpu, 3, m3, sh, lm);
    }
}

static inline __attribute__((always_inline)) void nc(CPUState *cpu, int v, int sh, int lm)     /* NCS */
{
    luz(cpu, v, sh, lm); cor_luz(cpu, sh, lm); push_cor(cpu);
}
static inline __attribute__((always_inline)) void ncc(CPUState *cpu, int v, int sh, int lm)    /* NCCS */
{
    luz(cpu, v, sh, lm); cor_luz(cpu, sh, lm); cor_vezes_ir(cpu, sh, lm, 0); push_cor(cpu);
}
static inline __attribute__((always_inline)) void ncd(CPUState *cpu, int v, int sh, int lm)    /* NCDS */
{
    luz(cpu, v, sh, lm); cor_luz(cpu, sh, lm); cor_vezes_ir(cpu, sh, lm, 1); push_cor(cpu);
}

static inline __attribute__((always_inline)) void dpc(CPUState *cpu, uint32_t rgb, int sh, int lm) /* DPCS/DPCT */
{
    interpolar(cpu, (int64_t)(rgb & 0xFFu) << 16, (int64_t)((rgb >> 8) & 0xFFu) << 16,
               (int64_t)((rgb >> 16) & 0xFFu) << 16, sh, lm);
    push_cor(cpu);
}

/* ---- MVMVA ---- */
static inline __attribute__((always_inline)) void mvmva(CPUState *cpu, uint32_t cmd, int sh, int lm)
{
    unsigned mx = (cmd >> 17) & 3u, vv = (cmd >> 15) & 3u, cv = (cmd >> 13) & 3u;
    int32_t v[3], m[9], t[3] = { 0, 0, 0 };
    int i, l;

    if (vv == 3) { v[0] = (int32_t)D(9); v[1] = (int32_t)D(10); v[2] = (int32_t)D(11); }
    else { v[0] = lo16(D(vv * 2)); v[1] = hi16(D(vv * 2)); v[2] = lo16(D(vv * 2 + 1)); }

    if (mx == 3) {                               /* matriz "reservada" (lixo do hardware) */
        int32_t r = (int32_t)((D(6) & 0xFFu) << 4);
        m[0] = -r; m[1] = r; m[2] = (int32_t)D(8);
        m[3] = m[4] = m[5] = M(cpu, 0, 0, 2);
        m[6] = m[7] = m[8] = M(cpu, 0, 1, 1);
    } else {
        unsigned base = mx == 0 ? 0u : mx == 1 ? 8u : 16u;
        #pragma GCC unroll 9
        for (i = 0; i < 9; i++) m[i] = M(cpu, base, (unsigned)i / 3u, (unsigned)i % 3u);
    }
    if (cv == 0) { t[0] = (int32_t)C(5);  t[1] = (int32_t)C(6);  t[2] = (int32_t)C(7); }
    if (cv == 1) { t[0] = (int32_t)C(13); t[1] = (int32_t)C(14); t[2] = (int32_t)C(15); }
    if (cv == 2) { t[0] = (int32_t)C(21); t[1] = (int32_t)C(22); t[2] = (int32_t)C(23); }

    #pragma GCC unroll 9

    for (l = 0; l < 3; l++) {
        int64_t r;
        if (cv != 2 && g_gte_rapido && t_pequeno((uint32_t)t[0], (uint32_t)t[1], (uint32_t)t[2]))
            r = ((int64_t)t[l] * 4096) + prod3(m[l * 3], v[0], m[l * 3 + 1], v[1], m[l * 3 + 2], v[2]);
        else if (cv == 2) {
            /* bug do FC: a 1ª coluna só afeta as flags */
            int64_t lixo = A(l + 1, ((int64_t)t[l] * 4096) + mul32(m[l * 3], v[0]));
            (void)lim_ir(l + 1, (int32_t)(lixo >> sh), 0);
            r = A(l + 1, A(l + 1, mul32(m[l * 3 + 1], v[1])) + mul32(m[l * 3 + 2], v[2]));
        } else {
            r = A(l + 1, A(l + 1, A(l + 1, ((int64_t)t[l] * 4096) + mul32(m[l * 3], v[0]))
                + mul32(m[l * 3 + 1], v[1])) + mul32(m[l * 3 + 2], v[2]));
        }
        set_mac_ir(cpu, l + 1, r, sh, lm);
    }
}

static inline __attribute__((always_inline)) void gte_corpo(CPUState *cpu, uint32_t cmd);
static __attribute__((noinline)) void gte_execute_corpo(CPUState *cpu, uint32_t cmd) { gte_corpo(cpu, cmd); }
#include "prof.h"
unsigned long long g_prof_gte;
unsigned long g_prof_gte_n;
/* V45: perfil por comando (índice = cmd & 0x3F): quantos e quantos ciclos.
 * Só no ELF com log (o Makefile passa PS2_LOG_BLOCOS para o gte.o): no ELF
 * de jogar não custa nada a mais. */
unsigned long g_prof_gte_cmd_n[64];
unsigned long long g_prof_gte_cmd_c[64];
#if defined(PS2_LOG_BLOCOS) && PS2_LOG_BLOCOS > 0
#define GTE_PROF_CMD(cmd, dt) do { g_prof_gte_cmd_c[(cmd) & 0x3Fu] += (dt); g_prof_gte_cmd_n[(cmd) & 0x3Fu]++; } while (0)
#else
#define GTE_PROF_CMD(cmd, dt) do { (void)(cmd); (void)(dt); } while (0)
#endif
#define GTE_PROF_FIM(cmd, t0) do { uint32_t dt_ = (uint32_t)(prof_agora() - (t0)); \
    g_prof_gte += dt_; g_prof_gte_n++; GTE_PROF_CMD(cmd, dt_); } while (0)
void gte_execute(CPUState *cpu, uint32_t cmd)
{
    uint32_t t0 = prof_agora();
    gte_execute_corpo(cpu, cmd);
    GTE_PROF_FIM(cmd, t0);
}

static inline __attribute__((always_inline)) void gte_corpo(CPUState *cpu, uint32_t cmd)
{
    int sh = (cmd & (1u << 19)) ? 12 : 0;
    int lm = (cmd & (1u << 10)) ? 1 : 0;
    int i;

    FLAG = 0;
    switch (cmd & 0x3Fu) {
    case 0x01:                                                         /* RTPS */
        if (__builtin_expect(g_gte_rapido && t_pequeno(C(5), C(6), C(7)), 1)) rtp_rapido(cpu, 1, sh, lm);
        else rtp_lento(cpu, 0, sh, lm);
        break;
    case 0x30:                                                         /* RTPT */
        if (__builtin_expect(g_gte_rapido && t_pequeno(C(5), C(6), C(7)), 1)) rtp_rapido(cpu, 3, sh, lm);
        else rtp_lento(cpu, 1, sh, lm);
        break;
    case 0x06: {                                                       /* NCLIP */
        int32_t x0 = lo16(D(12)), y0 = hi16(D(12)), x1 = lo16(D(13)), y1 = hi16(D(13));
        int32_t x2 = lo16(D(14)), y2 = hi16(D(14));
        D(24) = (uint32_t)(int32_t)F(mul32(x0, y1) + mul32(x1, y2) + mul32(x2, y0)
                                     - mul32(x0, y2) - mul32(x1, y0) - mul32(x2, y1));
        break; }
    case 0x0C: {                                                       /* OP */
        int32_t d1 = M(cpu, 0, 0, 0), d2 = M(cpu, 0, 1, 1), d3 = M(cpu, 0, 2, 2);
        int32_t i1 = (int32_t)D(9), i2 = (int32_t)D(10), i3 = (int32_t)D(11);
        set_mac_ir(cpu, 1, mul32(i3, d2) - mul32(i2, d3), sh, lm);
        set_mac_ir(cpu, 2, mul32(i1, d3) - mul32(i3, d1), sh, lm);
        set_mac_ir(cpu, 3, mul32(i2, d1) - mul32(i1, d2), sh, lm);
        break; }
    case 0x10: dpc(cpu, D(6), sh, lm); break;                          /* DPCS */
    case 0x2A: for (i = 0; i < 3; i++) dpc(cpu, D(20), sh, lm); break; /* DPCT */
    case 0x11:                                                         /* INTPL */
        interpolar(cpu, (int64_t)(int32_t)D(9) * 4096, (int64_t)(int32_t)D(10) * 4096,
                   (int64_t)(int32_t)D(11) * 4096, sh, lm);
        push_cor(cpu);
        break;
    case 0x12: mvmva(cpu, cmd, sh, lm); break;                         /* MVMVA */
    case 0x13: ncd(cpu, 0, sh, lm); break;                             /* NCDS */
    case 0x16: for (i = 0; i < 3; i++) ncd(cpu, i, sh, lm); break;     /* NCDT */
    case 0x14:                                                         /* CDP */
        cor_luz(cpu, sh, lm); cor_vezes_ir(cpu, sh, lm, 1); push_cor(cpu); break;
    case 0x1B: ncc(cpu, 0, sh, lm); break;                             /* NCCS */
    case 0x3F: for (i = 0; i < 3; i++) ncc(cpu, i, sh, lm); break;     /* NCCT */
    case 0x1C:                                                         /* CC */
        cor_luz(cpu, sh, lm); cor_vezes_ir(cpu, sh, lm, 0); push_cor(cpu); break;
    case 0x1E: nc(cpu, 0, sh, lm); break;                              /* NCS */
    case 0x20: for (i = 0; i < 3; i++) nc(cpu, i, sh, lm); break;      /* NCT */
    case 0x28:                                                         /* SQR */
        for (i = 1; i <= 3; i++)
            set_mac_ir(cpu, i, mul32((int32_t)D(8 + i), (int32_t)D(8 + i)), sh, lm);
        break;
    case 0x29: cor_vezes_ir(cpu, sh, lm, 1); push_cor(cpu); break;     /* DCPL */
    case 0x2D: case 0x2E: {                                            /* AVSZ3 / AVSZ4 */
        int64_t m0;
        uint32_t s1 = D(17) & 0xFFFFu, s2 = D(18) & 0xFFFFu, s3 = D(19) & 0xFFFFu;
        if ((cmd & 0x3Fu) == 0x2D)
            m0 = F(mul32(lo16(C(29)), (int32_t)(s1 + s2 + s3)));
        else
            m0 = F(mul32(lo16(C(30)), (int32_t)((D(16) & 0xFFFFu) + s1 + s2 + s3)));
        D(24) = (uint32_t)(int32_t)m0;
        {
            int32_t o = (int32_t)(m0 >> 12);
            if (o < 0)      { FLAG |= 1u << 18; o = 0; }
            if (o > 0xFFFF) { FLAG |= 1u << 18; o = 0xFFFF; }
            D(7) = (uint32_t)o;
        }
        break; }
    case 0x3D:                                                         /* GPF */
        for (i = 1; i <= 3; i++)
            set_mac_ir(cpu, i, mul32((int32_t)D(8), (int32_t)D(8 + i)), sh, lm);
        push_cor(cpu);
        break;
    case 0x3E:                                                         /* GPL */
        for (i = 1; i <= 3; i++)
            set_mac_ir(cpu, i, (int64_t)((uint64_t)(int64_t)(int32_t)D(24 + i) << sh) +
                       mul32((int32_t)D(8), (int32_t)D(8 + i)), sh, lm);
        push_cor(cpu);
        break;
    default: break;                                                    /* comando inválido: nada */
    }
    if (FLAG & 0x7F87E000u) FLAG |= 0x80000000u;
    C(31) = FLAG;
}

/* V30: um wrapper por comando que o jogo usa (opcode constante). Com o
 * comando fixo o compilador resolve sf/lm/matriz/vetor/translação e corta
 * a decodificação e os desvios; o código gerado e o nativo chamam estes. */
#define GTE_K(x) void gte_k_##x(CPUState *cpu) { uint32_t t0 = prof_agora(); gte_corpo(cpu, 0x##x##u); \
    GTE_PROF_FIM(0x##x##u, t0); }
GTE_LISTA_K(GTE_K)
#undef GTE_K

/* Gancho do código gerado (precisão de vértices no PC): no PS2 não faz nada;
 * a escrita na RAM já foi feita pelo SWC2. */
void gte_precision_store_word(uint32_t addr, uint8_t reg)
{
    (void)addr; (void)reg;
}

#ifdef _EE
#include <stdio.h>
#include <string.h>
/* V29: custo de cada comando no console (boot) */
void gte_bench(void)
{
    static CPUState c;
    static const struct { uint32_t cmd; const char *nome; } t[] = {
        { 0x00080030u, "RTPT" }, { 0x00080001u, "RTPS" }, { 0x01400006u, "NCLIP" },
        { 0x0158002Du, "AVSZ3" }, { 0x0108041Bu, "NCCS" }, { 0x0118043Fu, "NCCT" },
        { 0x00080012u | (1u << 17), "MVMVA" }, { 0x00F80413u, "NCDS" }, { 0x0028001Eu, "NCS" },
    };
    unsigned i, k;
    {   /* autoteste: caminho rápido x caminho completo, estados aleatórios */
        static const uint32_t ops[] = {0x01,0x12,0x13,0x16,0x1B,0x1C,0x1E,0x20,0x30,0x3F,0x14};
        static CPUState a, b;
        uint32_t sem = 1234567u; unsigned long dif = 0;
        for (k = 0; k < 20000u; k++) {
            uint32_t cmd;
            for (i = 0; i < 32; i++) {
                sem = sem * 1103515245u + 12345u; a.gte_data[i] = sem ^ (sem >> 13);
                sem = sem * 1103515245u + 12345u; a.gte_ctrl[i] = sem ^ (sem >> 11);
                if (k & 1) { a.gte_data[i] &= 0x0FFF0FFFu; a.gte_ctrl[i] &= 0x0FFF0FFFu; }
            }
            for (i = 1; i < 12; i++) if (i == 1 || i == 3 || i == 5 || (i >= 8 && i <= 11))
                a.gte_data[i] = (uint32_t)(int32_t)(int16_t)a.gte_data[i];
            sem = sem * 1103515245u + 12345u;
            /* V45: bits 13..24 (cv, v, mx, sf do MVMVA); antes 17..24 deixava cv/v de fora */
            cmd = ops[(sem >> 8) % (sizeof ops / sizeof ops[0])] | (sem & 0x1FFE000u) | ((sem << 3) & (1u << 10));
            b = a;
            g_gte_rapido = 1; gte_execute_corpo(&a, cmd);
            g_gte_rapido = 0; gte_execute_corpo(&b, cmd);
            if (memcmp(a.gte_data, b.gte_data, sizeof a.gte_data) || memcmp(a.gte_ctrl, b.gte_ctrl, sizeof a.gte_ctrl)) {
                if (dif++ < 4) printf("bench: gte DIFERENTE cmd %08lx\n", (unsigned long)cmd);
            }
        }
        g_gte_rapido = dif == 0;   /* qualquer diferença: fica no caminho completo */
        g_gte_autoteste_dif = (long)dif;
        printf("bench: gte autoteste rapido x completo: 20000 casos, %lu diferencas (rapido %s)\n",
               dif, g_gte_rapido ? "ligado" : "DESLIGADO");
    }
    /* valores típicos de um modelo 3D: nada satura, nenhum caminho de overflow */
    for (i = 0; i < 6; i += 2) { c.gte_data[i] = (uint32_t)(uint16_t)(100 + i * 7) | ((uint32_t)(uint16_t)(-50 + i) << 16);
                                 c.gte_data[i + 1] = (uint32_t)(uint16_t)(30 - i); }
    c.gte_data[6] = 0x30808080u;
    c.gte_data[9] = 0x100u; c.gte_data[10] = 0x80u; c.gte_data[11] = 0x40u;
    c.gte_data[17] = 1000u; c.gte_data[18] = 1100u; c.gte_data[19] = 1200u;
    c.gte_ctrl[0] = 0x1000u; c.gte_ctrl[2] = 0x1000u; c.gte_ctrl[4] = 0x1000u;   /* RT = identidade */
    c.gte_ctrl[7] = 2000u;                                                        /* TRZ */
    c.gte_ctrl[8] = 0x0800u; c.gte_ctrl[10] = 0x0800u; c.gte_ctrl[12] = 0x0800u; /* L */
    c.gte_ctrl[13] = 0x100u; c.gte_ctrl[14] = 0x100u; c.gte_ctrl[15] = 0x100u;   /* BK */
    c.gte_ctrl[16] = 0x1000u; c.gte_ctrl[18] = 0x1000u; c.gte_ctrl[20] = 0x1000u; /* LR */
    c.gte_ctrl[24] = 160u << 16; c.gte_ctrl[25] = 120u << 16; c.gte_ctrl[26] = 0x200u;
    c.gte_ctrl[27] = (uint32_t)(uint16_t)-100; c.gte_ctrl[28] = 0x100000u;
    c.gte_ctrl[29] = 0x155u; c.gte_ctrl[30] = 0x100u;
    for (k = 0; k < sizeof(t) / sizeof(t[0]); k++) {
        uint32_t t0, t1;
        uint32_t d[32];
        memcpy(d, c.gte_data, sizeof(d));
        gte_execute_corpo(&c, t[k].cmd);
        __asm__ volatile("mfc0 %0, $9" : "=r"(t0));
        for (i = 0; i < 256u; i++) { c.gte_data[17] = d[17]; c.gte_data[18] = d[18]; c.gte_data[19] = d[19];
                                     c.gte_data[9] = d[9]; c.gte_data[10] = d[10]; c.gte_data[11] = d[11];
                                     gte_execute_corpo(&c, t[k].cmd); }
        __asm__ volatile("mfc0 %0, $9" : "=r"(t1));
        printf("bench: gte %s %lu ciclos\n", t[k].nome, (unsigned long)((t1 - t0) / 256u));
    }
    {   /* V30: autoteste especializado x genérico em todos os comandos da lista */
        static CPUState a, b2;
        uint32_t sem = 99u; unsigned long dif = 0, casos = 0;
        unsigned r;
#define GTE_T(x) for (r = 0; r < 400u; r++) { \
            for (i = 0; i < 32; i++) { sem = sem * 1103515245u + 12345u; a.gte_data[i] = sem ^ (sem >> 9); \
                sem = sem * 1103515245u + 12345u; a.gte_ctrl[i] = sem ^ (sem >> 7); \
                if (r & 1) { a.gte_data[i] &= 0x0FFF0FFFu; a.gte_ctrl[i] &= 0x0FFF0FFFu; } } \
            for (i = 8; i < 12; i++) a.gte_data[i] = (uint32_t)(int32_t)(int16_t)a.gte_data[i]; \
            a.gte_data[1] = (uint32_t)(int32_t)(int16_t)a.gte_data[1]; a.gte_data[3] = (uint32_t)(int32_t)(int16_t)a.gte_data[3]; \
            a.gte_data[5] = (uint32_t)(int32_t)(int16_t)a.gte_data[5]; \
            b2 = a; gte_k_##x(&a); gte_execute_corpo(&b2, 0x##x##u); casos++; \
            if (memcmp(a.gte_data, b2.gte_data, sizeof a.gte_data) || memcmp(a.gte_ctrl, b2.gte_ctrl, sizeof a.gte_ctrl)) \
                { if (dif++ < 4) printf("bench: gte_k DIFERENTE %s\n", #x); } }
        GTE_LISTA_K(GTE_T)
#undef GTE_T
        printf("bench: gte especializados x generico: %lu casos, %lu diferencas\n", casos, dif);
    }
    {   /* V30: especializados (opcode constante) */
        uint32_t t0, t1;
        __asm__ volatile("mfc0 %0, $9" : "=r"(t0));
        for (i = 0; i < 256u; i++) gte_k_486012(&c);
        __asm__ volatile("mfc0 %0, $9" : "=r"(t1));
        printf("bench: gte MVMVA especializado %lu ciclos (com medicao)\n", (unsigned long)((t1 - t0) / 256u));
        __asm__ volatile("mfc0 %0, $9" : "=r"(t0));
        for (i = 0; i < 256u; i++) { c.gte_data[17] = 1000u; c.gte_data[18] = 1100u; c.gte_data[19] = 1200u; gte_k_280030(&c); }
        __asm__ volatile("mfc0 %0, $9" : "=r"(t1));
        printf("bench: gte RTPT especializado %lu ciclos (com medicao)\n", (unsigned long)((t1 - t0) / 256u));
    }
}
#endif
