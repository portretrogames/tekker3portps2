/*
 * ram.h — RAM principal do PS1 (2 MB) dentro do PS2.
 *
 * O PS1 e o EE do PS2 são little-endian e o PS1 exige que acessos de 16 e
 * 32 bits sejam alinhados (desalinhado gera exceção no console original).
 * Por isso a leitura/escrita é feita direto na memória, sem montar byte a
 * byte. Endereços desalinhados são alinhados para baixo (não acontecem em
 * código correto; assim evitamos travar o EE com exceção de alinhamento).
 *
 * Mapa: a RAM de 2 MB se repete 4 vezes no intervalo físico 0x000000-0x7FFFFF
 * e aparece nos segmentos KUSEG (0x0...), KSEG0 (0x8...) e KSEG1 (0xA...).
 */
#ifndef PS2_RAM_H
#define PS2_RAM_H

#include <stdint.h>

#define PS2_RAM_SIZE         0x00200000u
#define PS2_RAM_MIRROR_SIZE  0x00800000u
#define PS2_RAM_MASK         (PS2_RAM_SIZE - 1u)

/* Tipos com may_alias: acesso direto sem violar strict aliasing. */
typedef uint16_t ps2_u16a __attribute__((may_alias));
typedef uint32_t ps2_u32a __attribute__((may_alias));

/*
 * V56: a RAM e o scratchpad do PS1 ficam numa "janela" só (ram.c):
 *   g_ps2_janela + 0x000000  RAM (2 MB)            = g_ps1_ram
 *   g_ps2_janela + 0x800000  scratchpad (1 KB)     = g_ps2_scratchpad
 * Assim "endereço & 0xFFFFFF" serve para os dois (0x80xxxxxx -> 0x0xxxxx,
 * 0x1F8000xx -> 0x8000xx) sem testar qual é: uma soma e um AND por acesso
 * (runtime/janela.h, usado pelas rotinas nativas). Os 6 MB do meio não são
 * usados (o EE tem 32 MB). g_ps1_ram e g_ps2_scratchpad são macros para
 * dentro dela: o resto do código continua igual.
 * O alinhamento declarado aqui faz o gcc usar lw/sw (e não lwl+lwr/swl+swr)
 * nos acessos a endereço fixo do código gerado (variáveis globais do jogo).
 */
#define PS2_JANELA_SPAD  0x00800000u
#define PS2_JANELA_TAM   (PS2_JANELA_SPAD + 0x400u)
#define PS2_JANELA_MASK  0x00FFFFFFu
/*
 * Lugar no cache de dados do EE (8 KB, 2 vias: 64 conjuntos de 64 bytes; o
 * conjunto é o endereço módulo 4 KB). A janela é alinhada em 4 KB: o conjunto
 * de cada dado do PS1 passa a depender só do endereço dele no PS1 (scratchpad
 * = conjuntos 0 a 15) e não muda de uma compilação para outra. Os dados do
 * port usados o tempo todo ficam num bloco também alinhado (ram.c):
 *   + PS2_CPU_NO_CACHE      estado da CPU do PS1, registradores e GTE
 *                           (conjuntos 20 a 29), longe do scratchpad, com
 *                           que as rotinas 3D alternam o tempo todo
 *   + PS2_COD_PAG_NO_CACHE  g_cod_pag (conjuntos 32 a 39)
 * Os números com 0x e sem "u": entram no texto do montador (ram.c).
 * Motivo (console, PMC=1): entre duas compilações só o tamanho do código
 * tinha mudado, e as faltas de cache de dados nas rotinas nativas foram de
 * ~5,5 para ~11 por polígono (na lista, de ~7,5 para ~12): o código encolheu
 * 1,2 MB, todos os dados mudaram de endereço e passaram a disputar os mesmos
 * conjuntos.
 */
#define PS2_CPU_NO_CACHE     0x500
#define PS2_COD_PAG_NO_CACHE 0x800
extern uint8_t g_ps2_janela[PS2_JANELA_TAM] __attribute__((aligned(4096)));
/* RAM e scratchpad são nomes para dentro da janela, com o MESMO objeto-base.
 * (Até a revisão da V56 eram apelidos do montador declarados como arrays
 * distintos: o gcc supõe que objetos distintos não se sobrepõem e podia
 * reordenar uma escrita na pilha pela janela e uma leitura do mesmo lugar
 * por g_ps1_ram.) */
#define g_ps1_ram        g_ps2_janela
#define g_ps2_scratchpad (g_ps2_janela + PS2_JANELA_SPAD)

/*
 * Detecção de código sobrescrito: toda escrita que cai na área do
 * executável original (onde existe código compilado) marca um "grânulo"
 * de 64 bytes como sujo. O despacho (stubs.c) só usa o código compilado
 * se a RAM ainda for igual ao executável original nesse trecho; senão o
 * trecho (overlay carregado do CD) vai para o interpretador.
 */
extern uint32_t g_cod_ini, g_cod_tam;      /* offset na RAM e tamanho do EXE */
extern uint8_t  g_cod_sujo[];              /* 1 bit por 64 bytes */
#define PS2_COD_SUJO_BYTES (PS2_RAM_SIZE / 512u)   /* tamanho de g_cod_sujo (codigo.c) */
void ps2_cod_registrar(uint32_t endereco, uint32_t tamanho);
extern uint32_t g_cod_ver;                 /* V31: sobe quando um grânulo limpo suja */

/*
 * V56: teste curto em toda escrita do código gerado. g_cod_pag diz, por
 * página de 4 KB da RAM, se ela tem código compilado; só então chama a
 * marcação de verdade (codigo.c). Antes cada escrita trazia em linha o teste
 * da faixa do executável (com 2 leituras de g_cod_ini/g_cod_tam) e o do mapa
 * de grânulos: ~17 instruções em toda escrita em variável global do jogo
 * (os dados ficam dentro da imagem do executável). O resultado para o
 * despacho é o mesmo: ele só consulta grânulos de faixas de código.
 */
#define PS2_COD_PAG_BITS 12u
extern uint8_t g_cod_pag[PS2_RAM_SIZE >> PS2_COD_PAG_BITS];
void ps2_cod_marcar_lento(uint32_t addr);
/* Restringe o mapa às páginas que têm código: faixas = pares {início, tamanho}
 * (a tabela do despacho gerado). Sem esta chamada vale o executável inteiro. */
void ps2_cod_so_codigo(const uint32_t *faixas, uint32_t n);

static inline __attribute__((always_inline)) void ps2_cod_marcar(uint32_t addr)
{
    if (__builtin_expect(g_cod_pag[(addr & PS2_RAM_MASK) >> PS2_COD_PAG_BITS], 0))
        ps2_cod_marcar_lento(addr);
}

/* V48: marca todos os grânulos de código tocados por [addr, addr+bytes)
 * (offset na RAM, sem dar a volta nos 2 MB). Corta a faixa ao trecho do
 * executável e anda pelo início de cada grânulo, contado a partir de
 * g_cod_ini: vale para qualquer alinhamento e para o último grânulo
 * parcial; faixa toda fora do código (o caso comum) sai logo. */
static inline void ps2_cod_marcar_faixa(uint32_t addr, uint32_t bytes)
{
    uint32_t ini = addr & PS2_RAM_MASK, fim = ini + bytes, o;
    if (!bytes || fim <= g_cod_ini || ini >= g_cod_ini + g_cod_tam) return;
    if (ini < g_cod_ini) ini = g_cod_ini;
    if (fim > g_cod_ini + g_cod_tam) fim = g_cod_ini + g_cod_tam;
    for (o = (ini - g_cod_ini) & ~63u; o < fim - g_cod_ini; o += 64u)
        ps2_cod_marcar_lento(g_cod_ini + o);    /* a faixa já foi cortada ao executável */
}

/*
 * Verdadeiro se o endereço cai na RAM (ou espelhos).
 * Segmentos válidos (bits 31..29): 0 = KUSEG baixo, 4 = KSEG0, 5 = KSEG1.
 * A máscara 0x31 tem os bits 0, 4 e 5 ligados.
 */
static inline __attribute__((always_inline)) int ps2_ram_hit(uint32_t addr)
{
    return ((0x31u >> (addr >> 29)) & 1u) &&
           ((addr & 0x1FFFFFFFu) < PS2_RAM_MIRROR_SIZE);
}

/* Acessos diretos — chamar só depois de ps2_ram_hit(). */
static inline __attribute__((always_inline)) uint8_t ps2_ram_get8(uint32_t addr)
{
    return g_ps1_ram[addr & PS2_RAM_MASK];
}
static inline __attribute__((always_inline)) uint16_t ps2_ram_get16(uint32_t addr)
{
    return *(const ps2_u16a *)&g_ps1_ram[addr & (PS2_RAM_MASK & ~1u)];
}
static inline __attribute__((always_inline)) uint32_t ps2_ram_get32(uint32_t addr)
{
    return *(const ps2_u32a *)&g_ps1_ram[addr & (PS2_RAM_MASK & ~3u)];
}
static inline __attribute__((always_inline)) void ps2_ram_set8(uint32_t addr, uint8_t v)
{
    g_ps1_ram[addr & PS2_RAM_MASK] = v;
    ps2_cod_marcar(addr);
}
static inline __attribute__((always_inline)) void ps2_ram_set16(uint32_t addr, uint16_t v)
{
    *(ps2_u16a *)&g_ps1_ram[addr & (PS2_RAM_MASK & ~1u)] = v;
    ps2_cod_marcar(addr);
}
static inline __attribute__((always_inline)) void ps2_ram_set32(uint32_t addr, uint32_t v)
{
    *(ps2_u32a *)&g_ps1_ram[addr & (PS2_RAM_MASK & ~3u)] = v;
    ps2_cod_marcar(addr);
}

/* API com verificação (usada pelo runtime e pelos testes). */
void ps2_ram_clear(void);
uint8_t *ps2_ram_data(void);
int ps2_ram_offset(uint32_t address, uint32_t *offset);

int ps2_ram_read8(uint32_t address, uint8_t *value);
int ps2_ram_read16(uint32_t address, uint16_t *value);
int ps2_ram_read32(uint32_t address, uint32_t *value);
int ps2_ram_write8(uint32_t address, uint8_t value);
int ps2_ram_write16(uint32_t address, uint16_t value);
int ps2_ram_write32(uint32_t address, uint32_t value);

#endif
