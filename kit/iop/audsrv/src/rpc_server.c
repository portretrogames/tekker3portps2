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
 * audsrv iop rpc server.
 */

#include <stdio.h>
#include <thbase.h>
#include <thsemap.h>
#include <loadcore.h>
#include <sysmem.h>
#include <intrman.h>
#include <sifcmd.h>

#include <audsrv.h>
#include "audsrv_internal.h"
#include "common.h"
#include "rpc_client.h"
#include "debug_printf.h"

/* rpc server variables */
/** buffer for RPC DMA */
static int rpc_buffer[18000/4] __attribute__((__aligned__(64)));
/** RPC thread variables */
static SifRpcDataQueue_t qd;
/** RPC thread variables */
static SifRpcServerData_t sd0;
/* kit 0.3.0: servidor da SPU do PS1 (ps1spu.c), com buffer próprio */
static SifRpcServerData_t sd_ps1;
static int ps1_buffer[16640/4] __attribute__((__aligned__(64)));
/* o pacote do EE (até PS1SPU_PALAVRAS palavras) tem que caber */
typedef char ps1_buffer_cabe[(sizeof(ps1_buffer) >= PS1SPU_PALAVRAS * 4) ? 1 : -1];
static void *rpc_ps1(int func, unsigned *data, int size)
{
	(void)func;
	return ps1spu_comandos((u32 *)data, size);
}


/** RPC command handler.
 * @param func     command (one of AUDSRV_x)
 * @param data     pointer to data array
 * @param size     size of data array (in bytes)
 * @returns value depends on function invoked
 *
 * This is a single rpc handler, it unpacks the data array and calls
 * local functions.
 */
static void *rpc_command(int func, unsigned *data, int size)
{
	int ret;

	/* DPRINTF("rpc command %d\n", func); */
	switch(func)
	{
		case AUDSRV_INIT:
		ret = audsrv_init();
		/* kit 0.2.1: cliente dos callbacks só com o init certo (e uma vez só,
		 * ver initialize_rpc_client) */
		if (ret == AUDSRV_ERR_NOERROR && initialize_rpc_client() < 0)
			ret = AUDSRV_ERR_OUT_OF_MEMORY;
		break;

		case AUDSRV_FORMAT_OK:
		ret = audsrv_format_ok(data[0], data[1], data[2]);
		break;

		case AUDSRV_SET_FORMAT:
		ret = audsrv_set_format(data[0], data[1], data[2]);
		break;

		case AUDSRV_WAIT_AUDIO:
		ret = audsrv_wait_audio(data[0]);
		break;

		case AUDSRV_PLAY_AUDIO:
		/* Mudança do kit (Retrogames, 20/11/2026): o tamanho declarado em
		 * data[0] não passa do que chegou (size - 4); sem nem o cabeçalho,
		 * nada (data[0] não é lido). */
		{
			/* 0.2.1: pacote sem cabeçalho ou sem amostras não liga a
			 * reprodução (audsrv_play_audio liga antes de copiar) */
			if (size < 4)
			{
				ret = -AUDSRV_ERR_ARGS;
			}
			else
			{
				int n = (int)data[0];
				if (n > size - 4) n = size - 4;
				ret = n > 0 ? audsrv_play_audio((const char *)&data[1], n) : 0;
			}
		}
		break;

		case AUDSRV_STOP_AUDIO:
		ret = audsrv_stop_audio();
		break;

		case AUDSRV_SET_VOLUME:
		ret = audsrv_set_volume(data[0]);
		break;

		case AUDSRV_QUIT:
		ret = audsrv_quit();
		break;

		case AUDSRV_PLAY_CD:
		ret = audsrv_play_cd(data[0]);
		break;

		case AUDSRV_STOP_CD:
		ret = audsrv_stop_cd();
		break;

		case AUDSRV_GET_CDPOS:
		ret = audsrv_get_cdpos();
		break;

		case AUDSRV_GET_TRACKPOS:
		ret = audsrv_get_trackpos();
		break;

		case AUDSRV_SET_THRESHOLD:
		ret = audsrv_set_threshold(data[0]);
		break;

		case AUDSRV_GET_NUMTRACKS:
		ret = audsrv_get_numtracks();
		break;

		case AUDSRV_GET_TRACKOFFSET:
		ret = audsrv_get_track_offset(data[0]);
		break;

		case AUDSRV_PLAY_SECTORS:
		ret = audsrv_cd_play_sectors(data[0], data[1]);
		break;

		case AUDSRV_GET_CD_STATUS:
		ret = audsrv_get_cd_status();
		break;

		case AUDSRV_GET_CD_TYPE:
		ret = audsrv_get_cd_type();
		break;

		case AUDSRV_PAUSE_CD:
		ret = audsrv_cd_pause();
		break;

		case AUDSRV_RESUME_CD:
		ret = audsrv_cd_resume();
		break;

		case AUDSRV_INIT_ADPCM:
		ret = audsrv_adpcm_init();
		break;

		case AUDSRV_LOAD_ADPCM:
		return audsrv_load_adpcm((u32*)data[0], data[1], data[2]);

		case AUDSRV_PLAY_ADPCM:
		ret = audsrv_ch_play_adpcm(data[0], data[1]);
		break;

		case AUDSRV_IS_ADPCM_PLAYING:
		ret = audsrv_is_adpcm_playing(data[0], data[1]);
		break;

		case AUDSRV_FREE_ADPCM:
		ret = free_sample(data[0]);
		break;

		case AUDSRV_SET_ADPCM_VOL:
		ret = audsrv_adpcm_set_volume(data[0], data[1], data[2]);
		break;

		case AUDSRV_AVAILABLE:
		ret = audsrv_available();
		break;

		case AUDSRV_QUEUED:
		ret = audsrv_queued();
		break;


		default:
		ret = -1;
		break;
	}

	data[0] = ret;
	return data;
}

/** RPC listener thread
 * @param arg   not used

 * This is the main RPC thread. Nothing fancy here.
 */
static void rpc_server_thread(void *arg)
{
	(void)arg;

	sceSifInitRpc(0);

	DPRINTF("creating rpc server\n");

	sceSifSetRpcQueue(&qd, GetThreadId());
	sceSifRegisterRpc(&sd0, AUDSRV_IRX, (void *)rpc_command, rpc_buffer, NULL, NULL, &qd);
	sceSifRpcLoop(&qd);
}

/* kit 0.3.0: servidor da SPU do PS1 numa thread e fila próprias (com os
 * dois servidores na mesma fila a resposta do PCM se perdia no PCSX2) */
static SifRpcDataQueue_t qd_ps1;
static void rpc_ps1_thread(void *arg)
{
	(void)arg;
	sceSifInitRpc(0);
	sceSifSetRpcQueue(&qd_ps1, GetThreadId());
	sceSifRegisterRpc(&sd_ps1, AUDSRV_PS1_SID, (void *)rpc_ps1, ps1_buffer, NULL, NULL, &qd_ps1);
	sceSifRpcLoop(&qd_ps1);
}

int initialize_rpc_thread()
{
	int rpc_tid = create_thread(rpc_server_thread, 40, 0);
	DPRINTF("rpc server thread 0x%x started\n", rpc_tid);
	if (rpc_tid > 0 && create_thread(rpc_ps1_thread, 41, 0) <= 0)
		DPRINTF("ps1spu rpc thread failed\n");
	return (rpc_tid > 0);
}
