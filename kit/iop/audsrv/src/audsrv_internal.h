/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2005, ps2dev - http://www.ps2dev.org
# Licenced under GNU Library General Public License version 2
*/

/**
 * @file
 * audsrv IOP server
 */

#ifndef __AUDSRV_C_H__
#define __AUDSRV_C_H__

/** Find the minimum value between A and B */
#define MIN(a,b) ((a) <= (b)) ? (a) : (b)

//RPC service ID
#define	AUDSRV_IRX            0x870884e

//DMA channel allocation
#define AUDSRV_VOICE_DMA_CH	0
#define AUDSRV_BLOCK_DMA_CH	1

/* kit 0.3.0: SPU do PS1 no núcleo 0 (ps1spu.c). O número fica aqui porque o
 * <audsrv.h> que entra na compilação é o do PS2SDK. */
#ifndef AUDSRV_PS1_SID
#define AUDSRV_PS1_SID 0x8708850   /* servidor RPC próprio (o do audsrv é 0x870884e) */
#endif
extern volatile u16 ps1spu_mvol[2];
#define PS1SPU_PALAVRAS 4096   /* = KIT_SPU1_PALAVRAS (kit/som.h) */
void *ps1spu_comandos(u32 *p, int bytes);

#endif
