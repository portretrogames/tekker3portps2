#include <stdio.h>
#include <thbase.h>
#include <thsemap.h>
#include <loadcore.h>
#include <sysmem.h>
#include <intrman.h>
#include <sifcmd.h>
#include <libsd.h>
#include <sysclib.h>

#include <audsrv.h>
#include "audsrv_internal.h"
#include "rpc_client.h"

static SifRpcClientData_t client;
static int rpc_sema;

static int cliente_ok; /* kit 0.2.1: init repetido não cria outro semáforo */

int initialize_rpc_client(void)
{
	int s;
	if (cliente_ok)
		return 0;
	s = CreateMutex(IOP_MUTEX_UNLOCKED);
	if (s < 0)
		return -1;
	rpc_sema = s;
	cliente_ok = 1;

	client.server = NULL;
	while(sceSifBindRpc(&client, AUDSRV_IRX, 0) < 0 || client.server == NULL) DelayThread(500);
	return 0;
}

void deinitialize_rpc_client(void)
{
	if (!cliente_ok)
		return;
	cliente_ok = 0;
	memset(&client, 0, sizeof(client));
	DeleteSema(rpc_sema);
	rpc_sema = -1;
}

static void call_end_callback(void *end_param)
{
	(void)end_param;

	iSignalSema(rpc_sema);
}

void call_client_callback(int id)
{
	WaitSema(rpc_sema);
	sceSifCallRpc(&client, id, SIF_RPC_M_NOWAIT, NULL, 0, NULL, 0, &call_end_callback, NULL);
}
