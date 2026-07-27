// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#else
#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)
#include <proto/exec.h>
#endif

#include <devices/timer.h>
#include <exec/io.h>

#include <drv_timer.h>
#include <debug.h>

BOOL drv_timer_open(struct drv_timer *t)
{
	t->req = NULL;
	t->port = CreateMsgPort();
	if (!t->port)
		return FALSE;

	t->req = (struct timerequest *)CreateIORequest(t->port, sizeof(struct timerequest));
	if (!t->req)
	{
		DeleteMsgPort(t->port);
		t->port = NULL;
		return FALSE;
	}

	if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)t->req, 0) != 0)
	{
		Kprintf("[drv] %s: cannot open %s\n", __func__, TIMERNAME);
		DeleteIORequest((struct IORequest *)t->req);
		DeleteMsgPort(t->port);
		t->req = NULL;
		t->port = NULL;
		return FALSE;
	}

	return TRUE;
}

void drv_timer_close(struct drv_timer *t)
{
	if (!t->req)
		return;
	CloseDevice((struct IORequest *)t->req);
	DeleteIORequest((struct IORequest *)t->req);
	DeleteMsgPort(t->port);
	t->req = NULL;
	t->port = NULL;
}

static void drv_timer_load(struct drv_timer *t, ULONG ms)
{
	t->req->tr_node.io_Command = TR_ADDREQUEST;
	t->req->tr_time.tv_secs = ms / 1000UL;
	t->req->tr_time.tv_micro = (ms % 1000UL) * 1000UL;
}

void drv_timer_sleep_ms(struct drv_timer *t, ULONG ms)
{
	if (!t->req || ms == 0)
		return;
	drv_timer_load(t, ms);
	DoIO((struct IORequest *)t->req);
}

void drv_timer_arm_ms(struct drv_timer *t, ULONG ms)
{
	if (!t->req)
		return;
	drv_timer_load(t, ms);
	SendIO((struct IORequest *)t->req);
}

void drv_timer_consume(struct drv_timer *t)
{
	if (!t->req)
		return;
	if (CheckIO((struct IORequest *)t->req))
		WaitIO((struct IORequest *)t->req);
}

void drv_timer_cancel(struct drv_timer *t)
{
	if (!t->req)
		return;
	AbortIO((struct IORequest *)t->req);
	WaitIO((struct IORequest *)t->req);
}
