/*
 * ps1spu.c — SPU do PS1 tocada na SPU2 do PS2 (kit 0.3.0, Retrogames, 21/11/2026).
 *
 * O jogo do PS1 escreve nos registradores da SPU (0x1F801C00-0x1F801DFF) e
 * manda amostras ADPCM para a memória dela. O port (no EE) junta essas
 * escritas num pacote por quadro (servidor AUDSRV_PS1_SID) e aqui cada uma vira o
 * registrador equivalente do núcleo 0 da SPU2, que toca o mesmo ADPCM com o
 * mesmo envelope (ADSR) e o mesmo formato de volume. O núcleo 1 continua com
 * o PCM do audsrv (música XA decodificada no EE); a saída do núcleo 0 entra
 * no núcleo 1 pela entrada externa (AVOL, já em 0x7fff no audsrv.c).
 *
 * Diferenças tratadas:
 * - a memória da SPU do PS1 (512 KB) fica a partir de PS1SPU_BASE na SPU2;
 *   os endereços do PS1 (em unidades de 8 bytes) viram bytes + base;
 * - a SPU2 roda a 48 kHz e a do PS1 a 44,1 kHz: a afinação (PITCH) é
 *   multiplicada por 44100/48000 (os envelopes andam ~9% mais rápido);
 * - reverberação: ainda não (os registradores ficam guardados); o efeito do
 *   núcleo 0 fica desligado para não escrever na área das amostras. O Tekken 3
 *   não usa (log do port: 0 escritas em 1DC0-1DFF, EON sempre 0). Mapeamento
 *   conferido para quem precisar: ps2/README.md do Tekken 3, "Som".
 *
 * Servidor RPC próprio (AUDSRV_PS1_SID, rpc_server.c): o SIF RPC só aguenta
 * uma chamada em andamento por servidor, e o PCM do audsrv vai ao mesmo
 * tempo pelo outro (com um servidor só, a resposta de um ia para o outro e o
 * EE caía em _request_end).
 *
 * Pacote (palavras de 32 bits):
 *   1<<28 | i<<16 | valor   registrador: endereço 0x1F801C00 + 2*i (i < 512)
 *   2<<28 | (end/8)         dados: palavra seguinte = n bytes (múltiplo de 64,
 *                           não zero), depois n bytes; end múltiplo de 64
 *   0                       nada (pacote só para ler o ENDX/ENVX)
 * Resposta (64 bytes): [0] ENDX do núcleo 0 (vozes ligadas neste pacote
 * contam como não terminadas), [1..12] ENVX das 24 vozes (16 bits cada),
 * [13] pacotes tratados, [14] blocos de amostras que a SPU2 recusou.
 *
 * KON/KOFF: o pacote inteiro roda no IOP em microssegundos, menos que uma
 * amostra da SPU2 (~21 us), e sceSdSetSwitch grava os 24 bits: um segundo KON
 * no mesmo pacote zeraria o primeiro antes de a SPU2 ler. Por isso KON e KOFF
 * se acumulam no pacote todo e são gravados no fim (KOFF antes de KON). Se a
 * mesma voz pede KON e KOFF no mesmo pacote, o que veio antes é gravado ali,
 * e entre duas gravações há uma espera de uma amostra.
 *
 * Usa o canal 0 de transferência (AUDSRV_VOICE_DMA_CH), o mesmo do ADPCM do
 * audsrv: não use os dois juntos.
 */
#include <types.h>
#include <intrman.h>
#include <thbase.h>
#include <sysclib.h>
#include <libsd.h>
#include <audsrv.h>
#include "spu.h"
#include "audsrv_internal.h"

#define PS1SPU_BASE 0x80000u        /* bytes; 0x0-0x5000 são das entradas/saídas dos núcleos */
#define PS1SPU_TAM  0x80000u

volatile u16 ps1spu_mvol[2];        /* volume principal efetivo do núcleo 0 (audsrv.c update_volume) */

static int iniciado;
static u32 resposta[16] __attribute__((aligned(64)));
static u8 trans_buf[8192] __attribute__((aligned(64)));
static u32 pmon, non, eon;          /* máscaras de 24 bits montadas das metades */
/* Nada de u16 com valor inicial aqui: a .data do módulo ficava com tamanho
 * que não é múltiplo de 4 e o IOP do console travava ao carregar o audsrv
 * (zera a .bss a partir do fim da .data com escritas de 4 bytes; o PCSX2
 * aceita). spucnt começa em iniciar(). */
static u16 reverb[32], reverb_base, spucnt, evol[2], mvol[2];
static u32 pend_kon, pend_koff;     /* KON/KOFF acumulados no pacote */
static int gravou_chave;             /* já houve KON/KOFF (fica 1: custa só 25 us por pacote com chave) */
static int transferir(u32 end, u32 tam);

static void iniciar(void)
{
	u16 m;
	spucnt = 0xC000;                    /* SPU ligada e sem mudo até o jogo dizer */
	sceSdSetSwitch(SD_CORE_0 | SD_SWITCH_KOFF, 0xFFFFFF);
	sceSdSetSwitch(SD_CORE_0 | SD_SWITCH_PMON, 0);
	sceSdSetSwitch(SD_CORE_0 | SD_SWITCH_NON, 0);
	sceSdSetSwitch(SD_CORE_0 | SD_SWITCH_VMIXL, 0xFFFFFF);   /* todas as vozes na saída seca */
	sceSdSetSwitch(SD_CORE_0 | SD_SWITCH_VMIXR, 0xFFFFFF);
	sceSdSetSwitch(SD_CORE_0 | SD_SWITCH_VMIXEL, 0);         /* sem reverberação por enquanto */
	sceSdSetSwitch(SD_CORE_0 | SD_SWITCH_VMIXER, 0);
	sceSdSetCoreAttr(SD_CORE_0 | SD_CORE_EFFECT_ENABLE, 0);
	sceSdSetParam(SD_CORE_0 | SD_PARAM_EVOLL, 0);
	sceSdSetParam(SD_CORE_0 | SD_PARAM_EVOLR, 0);
	/* MMIX (como no PCSX2): bits 11/10 = vozes na saída seca L/R, 9/8 = na
	 * molhada. Núcleo 0: só as vozes. Núcleo 1: liga a entrada externa seca
	 * (bits 3/2, a saída do núcleo 0) sem mexer no resto do audsrv. */
	sceSdSetParam(SD_CORE_0 | SD_PARAM_MMIX, 0xF00);
	m = sceSdGetParam(SD_CORE_1 | SD_PARAM_MMIX);   /* 0xFFC depois do sceSdInit: já tem */
	sceSdSetParam(SD_CORE_1 | SD_PARAM_MMIX, m | 0x00C);
	/* 0.3.2: zera a memória do PS1 na SPU2. No console ela vem com o que o
	 * OPL/navegador deixou (o PCSX2 zera): uma voz numa área que o jogo não
	 * preencheu tocava ruído. Antes do volume principal, e o último bloco
	 * nunca passa do fim. Bloco que a SPU2 recusou conta em resposta[14] (o
	 * EE mostra no log); os comandos seguem: os dados do jogo que vêm no
	 * pacote não seriam mandados de novo. */
	{
		u32 a, t;
		memset(trans_buf, 0, sizeof(trans_buf));
		for (a = 0; a < PS1SPU_TAM; a += t) {
			t = PS1SPU_TAM - a < sizeof(trans_buf) ? PS1SPU_TAM - a : sizeof(trans_buf);
			if (transferir(a, t))
				sceSdVoiceTransStatus(AUDSRV_VOICE_DMA_CH, 1);
			else
				resposta[14]++;
		}
	}
	sceSdSetParam(SD_CORE_0 | SD_PARAM_MVOLL, ps1spu_mvol[0]);
	sceSdSetParam(SD_CORE_0 | SD_PARAM_MVOLR, ps1spu_mvol[1]);
	iniciado = 1;
}

/* MVOL efetivo: SPUCNT com a SPU desligada (bit 15) ou muda (bit 14 em 0) cala */
static void aplicar_mvol(void)
{
	int ligada = (spucnt & 0xC000u) == 0xC000u, estado;
	u16 l = ligada ? mvol[0] : 0, r = ligada ? mvol[1] : 0;
	/* o update_volume do audsrv (outra thread) também grava estes */
	CpuSuspendIntr(&estado);
	ps1spu_mvol[0] = l;
	ps1spu_mvol[1] = r;
	sceSdSetParam(SD_CORE_0 | SD_PARAM_MVOLL, l);
	sceSdSetParam(SD_CORE_0 | SD_PARAM_MVOLR, r);
	CpuResumeIntr(estado);
}

static void gravar_chave(u16 reg, u32 bits)
{
	if (gravou_chave) DelayThread(25);   /* uma amostra entre duas gravações */
	sceSdSetSwitch(SD_CORE_0 | reg, bits);
	gravou_chave = 1;
}

static void soltar_chaves(void)
{
	if (pend_koff) gravar_chave(SD_SWITCH_KOFF, pend_koff);
	if (pend_kon) gravar_chave(SD_SWITCH_KON, pend_kon);
	pend_kon = pend_koff = 0;
}

/* tipo 1 = KON, 2 = KOFF */
static void chave(u32 tipo, u32 bits)
{
	if (!bits) return;
	if (tipo == 1u) {
		if (bits & pend_koff) soltar_chaves();   /* KOFF antes deste KON */
		pend_kon |= bits;
	} else {
		if (bits & pend_kon) soltar_chaves();    /* KON antes deste KOFF */
		pend_koff |= bits;
	}
}

/* Clock do ruído (bits 8-13 do ATTR do núcleo 0). Não pelo sceSdSetCoreAttr
 * (SD_CORE_NOISE_CLK): no PCSX2 ele estragou o ATTR (c000 -> 80ff -> 41be),
 * ligando os bits de DMA, e toda sceSdVoiceTrans passou a devolver -1. */
#define ATTR_NUCLEO0 ((volatile u16 *)0xBF90019Au)
static void ruido(u32 clk)
{
	int estado;
	CpuSuspendIntr(&estado);
	*ATTR_NUCLEO0 = (u16)((*ATTR_NUCLEO0 & ~0x3F00u) | (clk << 8));
	CpuResumeIntr(estado);
}

/* Afinação: 0x1000 = taxa da SPU (44,1 kHz no PS1, 48 kHz na SPU2). */
static u16 afinacao(u16 p)
{
	u32 v = p > 0x3FFFu ? 0x3FFFu : p;
	return (u16)((v * 3763u) >> 12);   /* 44100/48000 = 0,91875 ~ 3763/4096 */
}

static void mascara(u32 *m, int alto, u16 v)
{
	*m = alto ? ((*m & 0xFFFFu) | ((u32)(v & 0xFFu) << 16)) : ((*m & 0xFF0000u) | v);
}

/* Registrador do PS1 (off = endereço - 0x1F801000, de 0xC00 a 0xFFE). */
static void registrador(u32 off, u16 v, u32 *kon)
{
	if (off < 0xD80u) {
		u32 voz = (off - 0xC00u) >> 4;
		u16 e = SD_VOICE(SD_CORE_0, voz);
		switch ((off >> 1) & 7u) {
		case 0: sceSdSetParam(e | SD_VPARAM_VOLL, v); break;
		case 1: sceSdSetParam(e | SD_VPARAM_VOLR, v); break;
		case 2: sceSdSetParam(e | SD_VPARAM_PITCH, afinacao(v)); break;
		case 3: sceSdSetAddr(e | SD_VADDR_SSA, PS1SPU_BASE + (u32)v * 8u); break;
		case 4: sceSdSetParam(e | SD_VPARAM_ADSR1, v); break;
		case 5: sceSdSetParam(e | SD_VPARAM_ADSR2, v); break;
		case 6: break;                  /* ENVX: o PS1 ignora a escrita */
		case 7: sceSdSetAddr(e | SD_VADDR_LSAX, PS1SPU_BASE + (u32)v * 8u); break;
		}
		return;
	}
	switch (off) {
	case 0xD80: mvol[0] = v; aplicar_mvol(); break;
	case 0xD82: mvol[1] = v; aplicar_mvol(); break;
	case 0xD84: evol[0] = v; break;     /* reverberação: guardado */
	case 0xD86: evol[1] = v; break;
	case 0xD88: chave(1u, v); *kon |= v; break;
	case 0xD8A: chave(1u, (u32)(v & 0xFFu) << 16); *kon |= (u32)(v & 0xFFu) << 16; break;
	case 0xD8C: chave(2u, v); break;
	case 0xD8E: chave(2u, (u32)(v & 0xFFu) << 16); break;
	case 0xD90: case 0xD92: mascara(&pmon, off == 0xD92, v); sceSdSetSwitch(SD_CORE_0 | SD_SWITCH_PMON, pmon & ~1u); break;
	case 0xD94: case 0xD96: mascara(&non, off == 0xD96, v); sceSdSetSwitch(SD_CORE_0 | SD_SWITCH_NON, non); break;
	case 0xD98: case 0xD9A: mascara(&eon, off == 0xD9A, v); break;   /* reverberação: guardado */
	case 0xDA2: reverb_base = v; break;
	case 0xDAA:
		spucnt = v;
		ruido((v >> 8) & 0x3Fu);
		aplicar_mvol();
		break;
	default:
		if (off >= 0xDC0u && off < 0xE00u) reverb[(off - 0xDC0u) >> 1] = v;
		break;
	}
}

/* Manda trans_buf para a SPU2; recusada (canal ocupado), tenta de novo com
 * pequenas esperas (até ~2 ms). */
static int transferir(u32 end, u32 tam)
{
	int tentativa;
	for (tentativa = 0; tentativa < 20; tentativa++) {
		if (sceSdVoiceTrans(AUDSRV_VOICE_DMA_CH, SD_TRANS_WRITE | SD_TRANS_MODE_DMA, trans_buf,
		                    (u32 *)(PS1SPU_BASE + end), tam) >= 0)
			return 1;
		DelayThread(100);
	}
	return 0;
}

void *ps1spu_comandos(u32 *p, int bytes)
{
	u32 n = bytes > 0 ? (u32)bytes / 4u : 0u, i = 0, kon = 0;
	int voz;
	if (!iniciado) iniciar();
	/* gravou_chave vem do pacote anterior: o EE manda pacotes na hora do KON
	 * (0.3.2), e dois pacotes seguidos podem chegar em menos de uma amostra */
	while (i < n) {
		u32 w = p[i++];
		u32 op = w >> 28;
		if (op == 0u) {
			continue;               /* nada */
		} else if (op == 1u) {
			registrador(0xC00u + ((w >> 16) & 0x1FFu) * 2u, (u16)w, &kon);
		} else if (op == 2u) {
			u32 end = (w & 0xFFFFu) * 8u, tam, palavras;
			if (i >= n) break;
			tam = p[i++];
			palavras = (tam + 3u) / 4u;
			if (!tam || palavras > n - i || tam > sizeof(trans_buf) || end + tam > PS1SPU_TAM || (tam & 63u) || (end & 63u))
				break;                  /* pacote estragado: para aqui */
			memcpy(trans_buf, &p[i], tam);
			if (!transferir(end, tam)) {
				resposta[14]++;         /* não liga voz com amostra velha: para aqui */
				break;
			}
			sceSdVoiceTransStatus(AUDSRV_VOICE_DMA_CH, 1);
			i += palavras;
		} else {
			break;
		}
	}
	soltar_chaves();
	resposta[0] = sceSdGetSwitch(SD_CORE_0 | SD_SWITCH_ENDX) & 0xFFFFFFu & ~kon;
	for (voz = 0; voz < 24; voz += 2)
		resposta[1 + voz / 2] = sceSdGetParam(SD_VOICE(SD_CORE_0, voz) | SD_VPARAM_ENVX) |
		                        ((u32)sceSdGetParam(SD_VOICE(SD_CORE_0, voz + 1) | SD_VPARAM_ENVX) << 16);
	resposta[13]++;
	(void)reverb; (void)reverb_base; (void)evol; (void)eon;
	return resposta;
}
