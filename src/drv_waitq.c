// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * drv_waitq.c - wait for a condition guarded by a SignalSemaphore (drv_waitq.h).
 */
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#else
#define __NOLIBBASE__
#include <proto/exec.h>
#endif

#include <exec/ports.h>

#include <memory.h>
#include <minlist.h>
#include <drv_waitq.h>

struct drv_waiter
{
	struct MinNode node;
	struct Task *task;
	ULONG sigmask; /* the bit the waiter sleeps on */
};

void drv_waitq_init(struct drv_waitq *wq)
{
	_NewMinList(&wq->waiters);
	memset(&wq->timer, 0, sizeof(wq->timer));
	wq->timer_open = FALSE;
}

void drv_waitq_cleanup(struct drv_waitq *wq, struct ExecBase *SysBase)
{
	if (wq->timer_open)
		CloseDevice((struct IORequest *)&wq->timer);
	wq->timer_open = FALSE;
}

/**
 * timer_ready() - Open the queue's timer if no wait has yet.
 * @wq:      the queue, its lock held
 * @SysBase: exec
 *
 * Return: TRUE if the timer is open.
 */
static BOOL timer_ready(struct drv_waitq *wq, struct ExecBase *SysBase)
{
	if (wq->timer_open)
		return TRUE;
	wq->timer.tr_node.io_Message.mn_Length = sizeof(wq->timer);
	wq->timer_open = OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)&wq->timer, 0) == 0;
	return wq->timer_open;
}

LONG drv_wait_event(struct drv_waitq *wq, struct ExecBase *SysBase, struct SignalSemaphore *lock, drv_wait_pred_t pred,
                    APTR a, APTR b, ULONG timeout_ms)
{
	if (pred(a, b))
		return DRV_WAIT_OK;
	if (!timer_ready(wq, SysBase))
		return DRV_WAIT_NO_RESOURCES;
	struct MsgPort *port = CreateMsgPort(); /* the timeout's reply port: its bit is the one the waiter sleeps on */
	if (port == NULL)
		return DRV_WAIT_NO_RESOURCES;

	struct timerequest timer;
	CopyMem(&wq->timer, &timer, sizeof(timer));
	timer.tr_node.io_Message.mn_ReplyPort = port;
	timer.tr_node.io_Command = TR_ADDREQUEST;
	timer.tr_time.tv_secs = timeout_ms / 1000UL;
	timer.tr_time.tv_micro = (timeout_ms % 1000UL) * 1000UL;
	SendIO((struct IORequest *)&timer);

	struct drv_waiter waiter;
	waiter.task = port->mp_SigTask;
	waiter.sigmask = 1UL << port->mp_SigBit;
	AddTail((struct List *)&wq->waiters, (struct Node *)&waiter.node);
	LONG rc = DRV_WAIT_OK;
	for (;;)
	{
		ReleaseSemaphore(lock);
		Wait(waiter.sigmask);
		ObtainSemaphore(lock);
		if (pred(a, b))
			break;
		if (CheckIO((struct IORequest *)&timer))
		{
			rc = DRV_WAIT_TIMEOUT;
			break;
		}
	}
	Remove((struct Node *)&waiter.node);
	/* nothing signals the bit any more: collect the timer, drop a wake-up still pending */
	if (!CheckIO((struct IORequest *)&timer))
		AbortIO((struct IORequest *)&timer);
	WaitIO((struct IORequest *)&timer);
	SetSignal(0, waiter.sigmask);
	DeleteMsgPort(port);
	return rc;
}

void drv_wake_up_all(struct drv_waitq *wq, struct ExecBase *SysBase)
{
	for (struct MinNode *n = wq->waiters.mlh_Head; n->mln_Succ != NULL; n = n->mln_Succ)
	{
		struct drv_waiter *w = (struct drv_waiter *)n;
		Signal(w->task, w->sigmask);
	}
}
