/* io.c — ver io.h. */
#include "io.h"
#include "cpu_state.h"
#include "ram.h"
#include "quente.h"
#include "cdrom.h"
#include "pad.h"
#include "mdec.h"
#include "gpu.h"
#include "som.h"     /* V47 */

#include <string.h>
#include <time.h>

#define IO_BASE  0x1F801000u
#define IO_SIZE  0x2000u          /* 0x1F801000-0x1F802FFF */

static uint8_t io_regs[IO_SIZE] __attribute__((aligned(16)));

static uint32_t i_stat, i_mask;

/* ---------------- relógio ---------------- */

#ifdef _EE
/* COUNT do COP0 do EE: 294,912 MHz. PS1: 33,8688 MHz -> razão ~8,7075.
 * Multiplica por 0,11484 (~= 7527/65536) para converter. */
static uint32_t ee_count(void)
{
    uint32_t c;
    __asm__ volatile("mfc0 %0, $9" : "=r"(c));
    return c;
}
static uint32_t ultimo_ee, acumulado_ps1;
static uint32_t resto;
uint32_t ps2_ps1_cycles(void)
{
    uint32_t agora = ee_count();
    uint32_t delta = agora - ultimo_ee;
    uint64_t t;
    ultimo_ee = agora;
    t = (uint64_t)delta * 7527u + resto;
    acumulado_ps1 += (uint32_t)(t >> 16);
    resto = (uint32_t)(t & 0xFFFFu);
    return acumulado_ps1;
}
#else
uint32_t ps2_ps1_cycles(void)   /* PC (testes): usa clock() */
{
    return (uint32_t)((uint64_t)clock() * 33868800u / CLOCKS_PER_SEC);
}
#endif

/* ---------------- registradores genéricos ---------------- */

static uint32_t reg_get(uint32_t off, unsigned w)
{
    if (w == 4) return *(ps2_u32a *)&io_regs[off & ~3u];
    if (w == 2) return *(ps2_u16a *)&io_regs[off & ~1u];
    return io_regs[off];
}
static void reg_set(uint32_t off, uint32_t v, unsigned w)
{
    if (w == 4) *(ps2_u32a *)&io_regs[off & ~3u] = v;
    else if (w == 2) *(ps2_u16a *)&io_regs[off & ~1u] = (uint16_t)v;
    else io_regs[off] = (uint8_t)v;
}

/* ---------------- interrupções ---------------- */

void ps2_irq_raise(unsigned bit) { i_stat |= 1u << bit; }
void ps2_irq_ack(unsigned bit)   { i_stat &= ~(1u << bit); }
uint32_t ps2_irq_pending(void)   { return i_stat & i_mask & 0x7FFu; }

/* ---------------- timers ---------------- */

typedef struct {
    uint32_t base;      /* ciclos PS1 no último reset */
    uint16_t modo, alvo;
    uint32_t marcas;    /* alvo (16 bits baixos) e overflow (altos) já vistos */
    uint32_t irq_marcas;/* idem, só dos eventos que geram IRQ */
    uint8_t  irq_feita; /* modo "one-shot": já gerou a IRQ */
    uint32_t atrasadas; /* V35: IRQs que o timer gerou e ainda não foram entregues */
} Timer;
static Timer tmr[3];

/* V35: o Tekken marca o ritmo do jogo pela IRQ do timer 2 (handler 80029784,
 * que libera o próximo passo em 8009bc5c), não pelo VBlank. Antes, se o alvo
 * fosse cruzado várias vezes entre duas conferências, só UMA IRQ era gerada e
 * as outras se perdiam -> o jogo andava a ~70% (cronômetro do round 40 s em
 * 56 s no console). Agora as que sobram ficam na fila e são entregues assim
 * que o jogo reconhece a anterior (I_STAT limpo). Teto para não virar
 * avalanche depois de uma parada longa (gravação, carregamento). */
#define TMR_MAX_ATRASO 64u
/* V43: 1 = descarta os cruzamentos do alvo que acontecem enquanto a própria
 * callback do timer roda (ver timer_write): 1 IRQ do timer 2 por VBlank, como
 * no PS1. 0 (padrão) = como na V42. Teste no PCSX2 (V43, luta automática):
 * com 1 o relógio do round andou 93-97% (sem: 99,2%), porque lá o passo 1 da
 * lógica termina perto do VBlank seguinte e, sem as IRQs extras, o passo de
 * recuperação às vezes não roda e o VBlank se perde. Só ligar quando a lista
 * de desenho sair de dentro da callback (o quadro começaria ~3 ms antes). */
/* V44: ligado para teste no console. Lá o passo 1 da luta termina ~3 ms
 * DEPOIS do VBlank seguinte (linha do tempo V42), então o passo de
 * recuperação continua acontecendo; e nos trechos de 1 passo por quadro
 * (abertura do round, KO: 19 s no port contra 12,1 s no PS1) as 2-3 voltas
 * extras da callback (~0,3 ms cada) caem logo no começo do quadro.
 * V45: confirmado no console (log da V44, 19/11/2026): o jogo vê 1 IRQ do
 * timer 2 por VBlank (120 em 120, antes ~250), relógio do round 99-100%, luta
 * em 2 VBlanks por quadro como na V42, interrupção 17% -> 15%. Fica ligado. */

static uint32_t timer_divisor(int t)
{
    uint32_t src = (tmr[t].modo >> 8) & 3u;
    if (t == 1 && (src & 1u)) return 2152u;        /* hblank (NTSC ~ 63,5 us) */
    if (t == 2 && (src & 2u)) return 8u;           /* sistema / 8 */
    if (t == 0 && (src & 1u)) return 5u;           /* dotclock (aprox.) */
    return 1u;
}

/* Contador do timer 't' como estaria 'adianto' unidades de contagem à frente. */
static uint16_t timer_valor_em(int t, uint32_t adianto)
{
    uint32_t c = (ps2_ps1_cycles() - tmr[t].base) / timer_divisor(t) + adianto;
    if ((tmr[t].modo & (1u << 3)) && tmr[t].alvo)   /* reset no alvo */
        c %= (uint32_t)tmr[t].alvo + 1u;
    return (uint16_t)c;
}
static uint16_t timer_valor(int t) { return timer_valor_em(t, 0u); }

/*
 * V56: esperas da libpad no VBlank. O tratador da libpad do jogo (80092D34)
 * conversa com os controles esperando tempo real pelo contador do timer 2:
 * 800951B8 guarda o contador e o prazo, 800951D8 e o laço em 80093584
 * comparam (pausas de 14-34 us entre bytes e ~95 us de prazo pelo ACK que
 * não vem: último byte e porta sem controle). No console: 0,5 ms por VBlank
 * (linha do tempo: "pad troca"/"pad ack").
 * Enquanto esse tratador roda (bios_hle.c liga), cada leitura do contador
 * devolve o timer PAD_SALTO unidades mais à frente que a anterior, como se
 * o tempo tivesse passado: as pausas e os prazos acabam na hora. O valor
 * continua dando a volta no alvo como o contador de verdade (a libpad soma
 * o alvo quando o contador volta: o salto tem de ser menor que o alvo).
 * Exceção — ACK a caminho ou recém-entregue (pad.c: ps2_pad_ack_ocupado):
 * a leitura não salta, e o tempo segue o real até o jogo ver o ACK (os laços
 * de 800932A0 e 80093478 testam o prazo ANTES de olhar o I_STAT). A leitura
 * do contador também não entrega ACK: ver ps2_pad_ack_ocupado.
 * O que fica adiantado: a pausa antes do 1º byte, o prazo do último byte
 * (que não tem ACK) e o da porta sem controle.
 * O timer em si não muda: ao sair do tratador o adianto some.
 */
#define PAD_SALTO 1024u     /* > maior prazo da libpad (430) */
static int pad_adianta;
static uint32_t pad_adianto;
void ps2_timer_pad_adianta(int ligar)
{
    pad_adianta = ligar;
    pad_adianto = 0;
}

static uint32_t timer_read(uint32_t off)
{
    int t = (int)((off - 0x100u) >> 4);
    switch (off & 0xFu) {
    case 0x0:
        if (t == 2 && pad_adianta) {
            /* sem reset no alvo o contador dá a volta em 65536: sempre cabe */
            int cabe = !((tmr[2].modo & (1u << 3)) && tmr[2].alvo) || tmr[2].alvo >= 2u * PAD_SALTO;
            if (cabe && !ps2_pad_ack_ocupado()) pad_adianto += PAD_SALTO;
            return timer_valor_em(2, pad_adianto);
        }
        return timer_valor(t);
    case 0x4: { uint32_t m = tmr[t].modo | (1u << 10);  /* sem IRQ pendente */
                tmr[t].modo &= ~((1u << 11) | (1u << 12)); return m; }
    case 0x8: return tmr[t].alvo;
    }
    return 0;
}

/* Marcas (quantas vezes o alvo / o overflow foram cruzados) no instante 'agora'. */
static void timer_marcas(int t, uint32_t agora, uint32_t *m_alvo, uint32_t *m_ovf)
{
    uint32_t alvo = tmr[t].alvo;
    uint32_t ticks = (agora - tmr[t].base) / timer_divisor(t);
    *m_alvo = 0; *m_ovf = 0;
    if ((tmr[t].modo & (1u << 3)) && alvo) {
        *m_alvo = (ticks + 1u) / (alvo + 1u);
    } else {
        *m_alvo = ticks >= alvo ? (ticks - alvo) / 0x10000u + 1u : 0u;
        *m_ovf = (ticks + 1u) / 0x10000u;
    }
}

/* Depois de escrever no contador ou no alvo: as marcas recomeçam do estado
 * atual, sem IRQ pelo recuo (auditoria V19 1.2: antes zerar o contador
 * disparava uma IRQ falsa na hora). */
static void timer_sincronizar(int t, uint32_t agora)
{
    uint32_t ma, mo, modo = tmr[t].modo;
    timer_marcas(t, agora, &ma, &mo);
    tmr[t].marcas = (ma & 0xFFFFu) | ((mo & 0xFFFFu) << 16);
    tmr[t].irq_marcas = (((modo & (1u << 4)) ? ma : 0u) & 0xFFFFu) |
                        ((((modo & (1u << 5)) ? mo : 0u) & 0xFFFFu) << 16);
}

static void timer_write(uint32_t off, uint32_t v)
{
    int t = (int)((off - 0x100u) >> 4);
    uint32_t agora = ps2_ps1_cycles();
    switch (off & 0xFu) {
    case 0x0: tmr[t].base = agora - (v & 0xFFFFu) * timer_divisor(t);
              timer_sincronizar(t, agora); break;
    case 0x4: /* V38: antes de reiniciar, gera as IRQs dos alvos já cruzados.
               * No PS1 elas já teriam acontecido antes desta escrita; aqui,
               * se a última conferência foi antes do cruzamento, se perdiam
               * (o Tekken rearma o timer 2 ~600x/s). */
              ps2_timers_poll();
              ps2_dma_poll();     /* V48: como antes de separar do timer */
              /* V43: escrita feita DENTRO da callback deste timer (o Tekken
               * desarma o timer 2 no fim de 80029784). No PS1 a callback acaba
               * bem antes do próximo alvo (1,19 ms); no port ela processa a
               * lista de desenho inteira (~3 ms no console) e o timer, ainda
               * em modo repetir, cruzava o alvo de novo: a IRQ voltava 3-4
               * vezes por quadro e cada volta somava 1 no contador 8009BC5C
               * (linha do tempo V42: 4-5 no teste 800295F4, no PS1 no máximo 2).
               * Esses cruzamentos não existem no PS1: são descartados. */
              {   extern unsigned g_ps2_tmr_servindo;
                  static const unsigned bit[3] = { IRQ_TMR0, IRQ_TMR1, IRQ_TMR2 };
                  if (g_ps2_tmr_servindo & (1u << t)) {
                      tmr[t].atrasadas = 0;
                      ps2_irq_ack(bit[t]);
                  }
              }
              tmr[t].modo = (uint16_t)(v & 0x3FFu); tmr[t].base = agora;
              tmr[t].marcas = 0; tmr[t].irq_marcas = 0; tmr[t].irq_feita = 0; break;
    case 0x8: tmr[t].alvo = (uint16_t)v; timer_sincronizar(t, agora); break;
    }
}

/*
 * Gera as IRQs dos timers. Alvo e overflow são contados separadamente
 * (achado F5): com "reset no alvo" o contador volta a 0 depois do alvo;
 * sem ele, o alvo é cruzado uma vez a cada 0x10000 contagens e o overflow
 * acontece em 0xFFFF. Marca também os bits 11 (alvo) e 12 (overflow) do modo.
 */
void ps2_timers_poll(void)
{
    static const unsigned irq[3] = { IRQ_TMR0, IRQ_TMR1, IRQ_TMR2 };
    uint32_t agora = ps2_ps1_cycles();
    int t;

    for (t = 0; t < 3; t++) {
        uint32_t modo = tmr[t].modo;
        uint32_t m_alvo, m_ovf, marcas;
        if (t == 2 && (modo & 1u) && ((modo >> 1) & 3u) % 3u == 0u)
            continue;                                   /* timer 2 parado */
        timer_marcas(t, agora, &m_alvo, &m_ovf);
        if (m_alvo != (tmr[t].marcas & 0xFFFFu)) modo |= 1u << 11;
        if (m_ovf != (tmr[t].marcas >> 16)) modo |= 1u << 12;
        tmr[t].modo = (uint16_t)modo;
        marcas = (((modo & (1u << 4)) ? m_alvo : 0u) & 0xFFFFu) |
                 ((((modo & (1u << 5)) ? m_ovf : 0u) & 0xFFFFu) << 16);
        if (marcas != (tmr[t].irq_marcas)) {
            /* quantos eventos novos (alvo + overflow) desde a última vez */
            uint32_t novos = ((marcas - tmr[t].irq_marcas) & 0xFFFFu) +
                             (((marcas >> 16) - (tmr[t].irq_marcas >> 16)) & 0xFFFFu);
            tmr[t].irq_marcas = marcas;
            if ((modo & (1u << 6)) || !tmr[t].irq_feita) {   /* repetir ou 1ª vez */
                if (!(modo & (1u << 6))) novos = 1u;
                tmr[t].irq_feita = 1;
                tmr[t].atrasadas += novos;
            }
        }
        if (tmr[t].atrasadas > TMR_MAX_ATRASO) tmr[t].atrasadas = TMR_MAX_ATRASO;
        if (tmr[t].atrasadas && !(i_stat & (1u << irq[t]))) {
            ps2_irq_raise(irq[t]);
            tmr[t].atrasadas--;
        }
        tmr[t].marcas = (m_alvo & 0xFFFFu) | ((m_ovf & 0xFFFFu) << 16);
    }
}

/* ---------------- DMA ---------------- */

static uint32_t dma_dicr;

/* Comandos que só desenham (podem ser pulados): preenchimento 0x02 e
 * polígonos/linhas/retângulos 0x20..0x7F. Estado (0xE1..0xE6) e
 * transferências de VRAM sempre são executados. */
static int desenho_pulavel(uint32_t op)
{
    return op == 0x02u || (op - 0x20u) < 0x60u;
}

/*
 * Orçamento da GPU. No PS1 a GPU desenha em paralelo com a CPU; aqui ela
 * roda no próprio EE e, como a libgpu dispara a próxima lista na interrupção
 * de fim de DMA, o desenho inteiro acontecia dentro da interrupção e o laço
 * principal do jogo ficava sem tempo (título lento, cronômetro parado).
 * Solução: a GPU pode usar até ORCAMENTO_GPU % do tempo; quando passa
 * disso, a lista seguinte só aplica estado e transferências (quadro pulado).
 */
/* Vale também com o GS: no PS2 real interpretar os comandos custa bem mais
 * que no PCSX2, e sem o orçamento o jogo inteiro fica lento (não só a imagem). */
#define ORCAMENTO_GPU 50u
#ifdef _EE
static int64_t  gpu_divida;            /* ciclos do EE acima do orçamento */
static uint32_t gpu_t_ini, gpu_janela_ini;
static uint32_t gpu_usado;             /* ciclos de GPU na janela (VBlank) atual */
static int      pular_quadro;
/* Chamado a cada VBlank: decide se o PRÓXIMO quadro inteiro é desenhado. */
void ps2_gpu_orcamento_vblank(void)
{
    uint32_t agora = ee_count(), janela = agora - gpu_janela_ini;
    gpu_janela_ini = agora;
    gpu_divida += (int64_t)gpu_usado - (int64_t)((uint64_t)janela * ORCAMENTO_GPU / 100u);
    if (gpu_divida < 0) gpu_divida = 0;
    gpu_usado = 0;
    /* tolerância de 1 VBlank inteiro: o jogo desenha em rajadas */
    pular_quadro = gpu_divida > (int64_t)janela;
}
int ps2_gpu_pular_lista(void)
{
    gpu_t_ini = ee_count();
    return pular_quadro;
}
void ps2_gpu_fim_lista(int pulou)
{
    gpu_usado += (uint32_t)(ee_count() - gpu_t_ini);
    (void)pulou;
}
#else
void ps2_gpu_orcamento_vblank(void) {}
int  ps2_gpu_pular_lista(void) { return 0; }
void ps2_gpu_fim_lista(int pulou) { (void)pulou; }
#endif

/*
 * Listas do DMA2 com ciclo. No PS1 o jogo nunca manda uma (a GPU ficaria
 * presa); se aparecer aqui, é efeito do port, e antes ela era percorrida até
 * 524.288 nós, reenviando os mesmos comandos centenas de milhares de vezes
 * (luta parada no console aos ~7 min 30 s, V11). Agora a cadeia é validada
 * antes (Floyd, só lê os cabeçalhos), cada nó é desenhado uma vez, o DMA é
 * concluído.
 */
/* assinatura da ÚLTIMA lista grande desenhada (ver dma_gpu) */
static struct { int valida; uint32_t madr, nos, hcab, geracao; } sig;
static struct { int ok; uint32_t madr, nos, hcab; } sig_cand;
int dedup_ligado = 1;                   /* 0 = sempre desenha (teste A/B) */
/* V40: o redesenho igual (mesma OT reenviada no 2º campo) é reconhecido só
 * pelos CABEÇALHOS dos nós (encadeamento + tamanhos), sem reler todos os
 * comandos: o hash completo custava ~5% do EE na luta. A limpeza da OT
 * (DMA6) e qualquer envio para a VRAM já invalidam a referência. Com 1,
 * volta a comparar também os comandos (V17). */
extern uint32_t g_ps2_vram_geracao;

static uint32_t proximo_no(uint32_t a)
{
    uint32_t c = ps2_ram_get32(a);
    return (c & 0x800000u) ? 0xFFFFFFFFu : (c & 0x1ffffcu);
}

/* 0 = sem ciclo; senão devolve 1 e o número de nós a desenhar (mu + lambda) */
static int lista_ciclica(uint32_t a0, uint32_t *mu_out, uint32_t *lambda_out)
{
    uint32_t t = a0, h = a0, mu = 0, lambda = 1, p, l;
    for (;;) {                                  /* Floyd */
        h = proximo_no(h); if (h == 0xFFFFFFFFu) return 0;
        h = proximo_no(h); if (h == 0xFFFFFFFFu) return 0;
        t = proximo_no(t);
        if (t == h) break;
    }
    t = a0;                                     /* início do ciclo */
    while (t != h) { t = proximo_no(t); h = proximo_no(h); mu++; }
    p = proximo_no(t);                          /* comprimento */
    for (l = 1; p != t; l++) p = proximo_no(p);
    lambda = l;
    *mu_out = mu; *lambda_out = lambda;
    return 1;
}

static int dma_gpu(uint32_t off, uint32_t madr, uint32_t bcr, uint32_t chcr)
{
    uint32_t a = madr & 0x1ffffcu;
    uint32_t modo = (chcr >> 9) & 3u;
    if (modo == 2u) {
        uint32_t nos = 0, limite = 0xFFFFFFFFu, mu, lambda;
        int pular;
        if (!(chcr & 1u)) return 0;      /* direção inválida */
        if (dedup_ligado && sig.valida && sig.madr == a && sig.geracao == g_ps2_vram_geracao) {
            /* caminho rápido: só os cabeçalhos, no máximo sig.nos nós */
            uint32_t h = a, k = 0, hc = 2166136261u, c;
            for (;;) {
                c = ps2_ram_get32(h);
                hc = (hc ^ c) * 16777619u;
                if (++k > sig.nos || (c & 0x800000u)) break;
                h = c & 0x1ffffcu;
            }
            if (k == sig.nos && (c & 0x800000u) && hc == sig.hcab) {
                reg_set(off - 8u, 0x00ffffffu, 4);
                return 1;
            }
        }
        {
            /*
             * Uma passada pela cadeia (Brent): detecta ciclo, conta os nós e
             * calcula o hash dos cabeçalhos (V40).
             */
            uint32_t t = a, h = a, poder = 1, lam = 0, cnt = 0, hcab = 2166136261u;
            int ciclo = 0;
            for (;;) {
                uint32_t c = ps2_ram_get32(h);
                cnt++;
                hcab = (hcab ^ c) * 16777619u;
                if (c & 0x800000u) break;
                h = c & 0x1ffffcu;
                if (h == t) { ciclo = 1; break; }
                if (poder == ++lam) { t = h; poder <<= 1; lam = 0; }
            }
            if (ciclo && lista_ciclica(a, &mu, &lambda)) {
                limite = mu + lambda;           /* cada nó uma vez */
            } else if (cnt >= 200u) {
                /*
                 * Redesenho da MESMA lista (modo entrelaçado 368x480: o jogo
                 * reenvia a OT atual a cada campo enquanto o quadro novo não
                 * fica pronto). Aqui a primeira vez já desenhou a tela
                 * inteira; repetir gastava 39-46% do EE. Só pula quando é
                 * exatamente a última lista grande desenhada (inteira, sem
                 * pulos do orçamento), com os mesmos cabeçalhos, sem limpeza
                 * da OT (DMA6) e sem envio/cópia para a VRAM desde então. Ver
                 * revisão do ChatGPT (REVISAO_LENTIDAO_LUTA_V16, P1). A
                 * comparação é o caminho rápido do começo da função; aqui só
                 * a lista desenhada agora vira a candidata.
                 */
                sig.valida = 0;
                sig_cand.madr = a; sig_cand.nos = cnt; sig_cand.hcab = hcab; sig_cand.ok = 1;
            } else
                sig_cand.ok = 0;
        }
        pular = ps2_gpu_pular_lista();
        {
        PS2_PILHA_EM(PS2_PILHA_LISTA);                         /* quente.h */
        for (;;) {
            uint32_t cabecalho, n, i;
            if (++nos > limite) break;
            cabecalho = ps2_ram_get32(a);
            n = cabecalho >> 24;
            if (pular && n && desenho_pulavel(ps2_ram_get32((a + 4u) & 0x1ffffcu) >> 24)) {
                a = (a + 4u * n) & 0x1ffffcu;
                n = 0;
            }
            if (n && a + 4u + 4u * n <= 0x200000u) {
                ps2_gpu_gp0_bloco((const uint32_t *)&g_ps1_ram[a + 4u], n);
                a = (a + 4u * n) & 0x1ffffcu;
            } else
                for (i = 0; i < n; ++i) {
                    a = (a + 4u) & 0x1ffffcu;
                    ps2_gpu_gp0(ps2_ram_get32(a));
                }
            if (cabecalho & 0x800000u) break;
            a = cabecalho & 0x1ffffcu;
        }
        ps2_gpu_fim_lista(pular);
        }
        if (sig_cand.ok) {                  /* desenhada agora: vira referência */
            sig.valida = !pular;            /* com pulos do orçamento não vale */
            sig.madr = sig_cand.madr; sig.nos = sig_cand.nos; sig.hcab = sig_cand.hcab;
            sig.geracao = g_ps2_vram_geracao;
            sig_cand.ok = 0;
        }
        reg_set(off - 8u, 0x00ffffffu, 4);
    } else if (modo <= 1u) {
        uint32_t palavras = bcr & 0xffffu;
        uint32_t blocos = bcr >> 16;
        uint64_t total;
        uint32_t passo = (chcr & 2u) ? (uint32_t)-4 : 4u;
        if (!palavras) palavras = 0x10000u;
        if (!blocos) blocos = 0x10000u;
        total = (uint64_t)palavras * (modo ? blocos : 1u);
        if (total > 0x1000000u) return 0;   /* bloco grande demais */
        /* V48: imagem para a VRAM direto da RAM, sem passar palavra por palavra */
        while ((chcr & 1u) && passo == 4u && total) {
            uint32_t ate_fim = (0x200000u - a) / 4u;
            uint32_t n = total < ate_fim ? (uint32_t)total : ate_fim;
            unsigned k = ps2_gpu_gp0_imagem(&g_ps1_ram[a], n);
            if (!k) break;
            total -= k;
            a = (a + 4u * k) & 0x1ffffcu;
        }
        while (total--) {
            if (chcr & 1u) ps2_gpu_gp0(ps2_ram_get32(a));
            else ps2_ram_set32(a, ps2_gpu_read());
            a = (a + passo) & 0x1ffffcu;
        }
        reg_set(off - 8u, a, 4);
    } else
        return 0;                       /* modo DMA inválido */
    return 1;
}

static uint32_t dma_palavras(uint32_t bcr, uint32_t chcr)
{
    uint32_t palavras = bcr & 0xFFFFu, blocos = bcr >> 16;
    if (palavras == 0) palavras = 0x10000u;
    if (((chcr >> 9) & 3u) == 1u) palavras *= blocos ? blocos : 1u;
    return palavras;
}

static void dma_ordering_table(uint32_t madr, uint32_t bcr)
{
    /* Canal 6 (OTC): limpa a ordering table de trás para a frente.
     * Cada entrada aponta para a anterior; a última é 0x00FFFFFF. */
    uint32_t n = bcr & 0xFFFFu;
    uint32_t a = madr & 0x1FFFFCu;
    if (n == 0) n = 0x10000u;
    /* V42: o DMA6 escreve n palavras de MADR para BAIXO (psx-spx, DMA6 OTC):
     * faixa [a-(n-1)*4, a]. A lista de referência só foi apagada se o início
     * dela (sig.madr) está nessa faixa: a - sig.madr <= (n-1)*4, ou seja,
     * a - sig.madr < n*4. Antes era "< n*4+4": as duas OTs do Tekken são
     * vizinhas (1024 entradas cada, topos 0x0A7584 e 0x0A8584, distância
     * exatamente n*4), então limpar a OT B invalidava a referência da OT A
     * sem ela ter sido tocada, e o redesenho seguinte (mesma lista, logo no
     * início do passo 1 do quadro) era processado inteiro de novo em metade
     * dos quadros da luta (~4,2 ms no PCSX2, linha do tempo V41d). */
    if (sig.valida && a - sig.madr < n * 4u) sig.valida = 0;
    if ((n - 1u) * 4u <= a) {
        /* não dá a volta nos 2 MB: escreve direto e marca cada grânulo de
         * 64 bytes uma vez (antes marcava a cada palavra) */
        uint32_t ini = a - (n - 1u) * 4u;
        ps2_u32a *p = (ps2_u32a *)&g_ps1_ram[ini];
        uint32_t k;
        p[0] = 0x00FFFFFFu;
        for (k = 1; k < n; k++) p[k] = ini + (k - 1u) * 4u;
        ps2_cod_marcar_faixa(ini, n * 4u);
        return;
    }
    while (n--) {
        ps2_ram_set32(a, n ? ((a - 4u) & 0x1FFFFCu) : 0x00FFFFFFu);
        a = (a - 4u) & 0x1FFFFCu;
    }
}

static void dma_irq(uint32_t canal)
{
    if (dma_dicr & (1u << (16 + canal))) {
        dma_dicr |= 1u << (24 + canal);
        if (dma_dicr & (1u << 23))
            ps2_irq_raise(IRQ_DMA);
    }
}

/*
 * V48: o fim do DMA1 (MDEC -> RAM) é avisado um pouco DEPOIS, como no PS1,
 * e não na hora. O tocador do vídeo pede a saída em 16 faixas por quadro e,
 * no aviso de cada uma (callback da IRQ do DMA), manda a faixa para a VRAM e
 * pede a próxima. Com o fim na hora, as 16 faixas rodavam numa única IRQ (o
 * tratador do libetc vê o flag de novo e repete): ~70 ms no PCSX2 com as
 * interrupções desligadas, o CD não era atendido e o drive esperava (o vídeo
 * perdia quadros). Os dados vão para a RAM na hora; só o aviso espera.
 */
#define DMA1_CICLOS_POR_PALAVRA 2u
static int      dma1_fim_pendente;
static uint32_t dma1_fim_quando;

static void dma1_concluir(void)
{
    reg_set(0x098, reg_get(0x098, 4) & ~((1u << 24) | (1u << 28)), 4);
    dma_irq(1);
}

static void dma1_agendar(uint32_t palavras)
{
    dma1_fim_quando = ps2_ps1_cycles() + palavras * DMA1_CICLOS_POR_PALAVRA;
    dma1_fim_pendente = 1;
}

/* V48: chamada a cada conferência de IRQ (irq_poll_corpo em bios_hle.c) */
void ps2_dma_poll(void)
{
    if (dma1_fim_pendente && (int32_t)(ps2_ps1_cycles() - dma1_fim_quando) >= 0) {
        dma1_fim_pendente = 0;
        dma1_concluir();
    }
}

static void dma_chcr_write(uint32_t off, uint32_t v)
{
    uint32_t canal = (off - 0x80u) >> 4;
    uint32_t madr = reg_get(off - 8u, 4), bcr = reg_get(off - 4u, 4);

    if (canal == 1) dma1_fim_pendente = 0;  /* nova escrita: o aviso antigo não vale */
    if (v & (1u << 24)) {                 /* start/busy */
        if (canal == 0)
            ps2_mdec_dma_in(madr, dma_palavras(bcr, v));
        else if (canal == 1) {
            uint32_t pal = dma_palavras(bcr, v);
            reg_set(off, v, 4);           /* ocupado até o aviso */
            /* sem dados suficientes, espera o MDEC (ps2_dma_mdec_saida_pronta) */
            if (ps2_mdec_dma_out(madr, pal)) dma1_agendar(pal);
            return;
        }
        else if (canal == 2 && !dma_gpu(off, madr, bcr, v)) {
            reg_set(off, v, 4);
            return;
        }
        else if (canal == 6)
            dma_ordering_table(madr, bcr);
        else if (canal == 3) {            /* CD-ROM -> RAM */
            uint32_t bs = bcr & 0xFFFFu, bc = bcr >> 16;
            ps2_cdrom_dma(madr, bs * (bc ? bc : 1u));
        }
        else if (canal == 4)              /* SPU: ainda não transfere (V47: só conta) */
            ps2_som_dma4(madr, bcr, v);
        v &= ~((1u << 24) | (1u << 28));
        dma_irq(canal);
    }
    reg_set(off, v, 4);
}

/* O MDEC terminou de entregar a saída que o DMA1 esperava (as 'palavras'
 * que faltavam): o aviso sai depois do tempo dessas palavras, como quando
 * os dados já estavam prontos no início. */
void ps2_dma_mdec_saida_pronta(uint32_t palavras)
{
    dma1_agendar(palavras);
}

static uint32_t dma_dicr_read(void);

static void dma_dicr_write(uint32_t v)
{
    uint32_t antes = dma_dicr_read() >> 31;
    /* bits 24-30 (flags) são limpos escrevendo 1 */
    uint32_t flags = (dma_dicr & ~v) & 0x7F000000u;
    dma_dicr = (v & 0x00FF803Fu) | flags;
    /* bit mestre passou de 0 para 1 na escrita: IRQ (achado F6) */
    if (!antes && (dma_dicr_read() >> 31))
        ps2_irq_raise(IRQ_DMA);
}

static uint32_t dma_dicr_read(void)
{
    uint32_t r = dma_dicr;
    if ((r & (1u << 15)) ||
        ((r & (1u << 23)) && (((r >> 16) & (r >> 24) & 0x7Fu) != 0)))
        r |= 1u << 31;
    return r;
}

/* ---------------- API ---------------- */

void ps2_io_init(void)
{
    memset(io_regs, 0, sizeof(io_regs));
    memset(tmr, 0, sizeof(tmr));
    i_stat = i_mask = dma_dicr = 0;
    dma1_fim_pendente = 0;
    ps2_cdrom_reset();
    ps2_pad_reset();
    ps2_mdec_reset();
    ps2_gpu_reset();
}

int ps2_io_hit(uint32_t address)
{
    uint32_t seg = address >> 29;
    uint32_t phys = address & 0x1FFFFFFFu;
    if (!(seg == 0u || seg == 4u || seg == 5u))
        return 0;
    return phys - IO_BASE < IO_SIZE;
}

uint32_t ps2_io_read(uint32_t address, unsigned w)
{
    uint32_t off = (address & 0x1FFFFFFFu) - IO_BASE;
    uint32_t v;

    /* V48: a leitura de I_STAT/DICR/CHCR NÃO conclui o DMA1 vencido: o
     * tratador de DMA da libetc relê o DICR em laço e voltava a fazer as 16
     * faixas do quadro numa IRQ só (vídeo da abertura de ~19 para ~15
     * quadros/s no PCSX2 e falhas no som). O fim sai nas conferências. */
    if (off == 0x070) { ps2_pad_tick(); v = i_stat; }
    else if (off == 0x074) v = i_mask;
    else if (off >= 0x0F4 && off < 0x0F8)       /* DICR: também em 8/16 bits */
        v = dma_dicr_read() >> (8u * (off - 0x0F4u));
    else if (off >= 0x100 && off < 0x130) v = timer_read(off & ~1u);
    else if (off == 0x814) v = ps2_gpu_status();
    else if (off == 0x810) v = ps2_gpu_read();
    else if (off == 0x820) v = ps2_mdec_read_data();
    else if (off == 0x824) v = ps2_mdec_read_status();
    else if (off == 0xDAE)                      /* SPUSTAT espelha SPUCNT */
        v = reg_get(0xDAA, 2) & 0x3Fu;
    /* V53: ENDX e ENVX vêm da SPU2 (spu.c); sem ela, ENDX = todas as vozes
     * "já terminaram" */
    else if (off >= 0xC00 && off < 0xE00 && ps2_som_spu_leitura(off, w, &v)) {}
    else if (off >= 0x040 && off < 0x050) v = ps2_pad_read(off, w);
    else if (off >= 0x800 && off < 0x804) {    /* CD-ROM */
        v = ps2_cdrom_read(off - 0x800u);
        if (w > 1) v |= (uint32_t)ps2_cdrom_read(off + 1u - 0x800u) << 8;
    }
    else v = reg_get(off, w);

    if (w == 2) v &= 0xFFFFu;
    else if (w == 1) v &= 0xFFu;
    return v;
}

void ps2_io_write(uint32_t address, uint32_t v, unsigned w)
{
    uint32_t off = (address & 0x1FFFFFFFu) - IO_BASE;

    if (off == 0x070) {                                 /* ack: escreve 0 */
        /* escrita de 8/16 bits só alcança os bits que escreve */
        i_stat &= v | (w == 1 ? ~0xFFu : w == 2 ? ~0xFFFFu : 0u);
        return;
    }
    if (off == 0x074) { i_mask = v & 0x7FFu; return; }
    if (off >= 0x0F4 && off < 0x0F8) {                 /* DICR */
        /* O libcd liga/desliga a IRQ do DMA3 escrevendo só o byte 2 (bits
         * 16-23, 1F8010F6) antes de cada transferência: só a do último setor
         * de um quadro do vídeo avisa no fim (callback 800858B8, "quadro
         * completo"). Antes 1F8010F6 caía num registrador qualquer, a IRQ do
         * DMA3 ficava sempre ligada e cada setor virava um quadro completo;
         * na 1a perda de setor o anel do vídeo travava e a abertura pulava
         * para a tela de seleção. Os bytes não escritos mantêm o valor; nos
         * flags (24-30, escrever 1 limpa) vai 0 para não reconhecer nada. */
        uint32_t sh = 8u * (off - 0x0F4u);
        uint32_t m = (w == 1 ? 0xFFu : w == 2 ? 0xFFFFu : 0xFFFFFFFFu) << sh;
        dma_dicr_write((dma_dicr & 0x00FFFFFFu & ~m) | ((v << sh) & m));
        return;
    }
    if (off >= 0x080 && off < 0x0F0 && (off & 0xFu) == 0x8u) {
        dma_chcr_write(off, v);
        return;
    }
    if (off >= 0x100 && off < 0x130) { timer_write(off & ~1u, v); return; }
    if (off >= 0x040 && off < 0x050) { ps2_pad_write(off, v, w); return; }
    if (off >= 0x800 && off < 0x804) { ps2_cdrom_write(off - 0x800u, (uint8_t)v); return; }
    if (off == 0x810) { ps2_gpu_gp0(v); return; }
    if (off == 0x814) { ps2_gpu_gp1(v); return; }
    if (off == 0x820) { ps2_mdec_write_data(v); return; }
    if (off == 0x824) { ps2_mdec_write_control(v); return; }
    if (off >= 0xC00u && off < 0x1000u)                 /* SPU: V47 só conta (som.c) */
        ps2_som_spu_escrita(off, v, w);
    reg_set(off, v, w);
}
