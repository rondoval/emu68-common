// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * drv_waitq.h - wait for a condition guarded by a SignalSemaphore, with a
 * timeout.
 *
 * A waiter sleeps with the caller's lock released, on the signal bit of a
 * message port it creates for the wait (never SIGF_SINGLE, which
 * ObtainSemaphore() uses).  The port also takes the reply of its timeout: a
 * copy of the queue's timerequest.  timer.device is opened once, at the
 * first wait that sleeps, and closed by drv_waitq_cleanup().
 *
 * Whoever changes the guarded state calls drv_wake_up_all() with the lock
 * held; every waiter re-checks its predicate under the lock.  The lock also
 * guards the waiter list and the timer: a waiter is on the list from before
 * its first release of the lock until after its last obtain, so no change is
 * missed and no bit is signalled after it is freed.  All state is
 * caller-owned: ROM-safe.
 */
#ifndef _DRV_WAITQ_H
#define _DRV_WAITQ_H

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/semaphores.h>
#include <devices/timer.h>

struct ExecBase;

struct drv_waitq
{
	struct MinList waiters;
	struct timerequest timer; /* opened at the first wait that sleeps; every wait sends a copy */
	BOOL timer_open;
};

/* TRUE when the condition holds; called with the lock held. */
typedef BOOL (*drv_wait_pred_t)(APTR a, APTR b);

enum drv_wait_result
{
	DRV_WAIT_OK = 0,
	DRV_WAIT_TIMEOUT = 1,
	DRV_WAIT_NO_RESOURCES = 2, /* no timer.device or no free signal bit */
};

/**
 * drv_waitq_init() - An empty queue, timer not yet open.
 * @wq: the queue
 */
void drv_waitq_init(struct drv_waitq *wq);

/**
 * drv_waitq_cleanup() - Close the queue's timer, if a wait opened it.
 * @wq:      the queue, no waiter on it
 * @SysBase: exec
 */
void drv_waitq_cleanup(struct drv_waitq *wq, struct ExecBase *SysBase);

/**
 * drv_wait_event() - Wait until a condition holds or a timeout passes.
 * @wq:         the queue
 * @SysBase:    exec
 * @lock:       held on entry and on return, released while sleeping
 * @pred:       the condition, checked first: one that holds already costs nothing
 * @a:          its first argument
 * @b:          its second argument
 * @timeout_ms: how long to wait at most
 *
 * Return: DRV_WAIT_OK, DRV_WAIT_TIMEOUT, or DRV_WAIT_NO_RESOURCES.
 */
LONG drv_wait_event(struct drv_waitq *wq, struct ExecBase *SysBase, struct SignalSemaphore *lock, drv_wait_pred_t pred,
                    APTR a, APTR b, ULONG timeout_ms);

/**
 * drv_wake_up_all() - Wake every waiter to re-check its condition.
 * @wq:      the queue
 * @SysBase: exec
 *
 * Lock held: never from an interrupt.
 */
void drv_wake_up_all(struct drv_waitq *wq, struct ExecBase *SysBase);

#endif /* _DRV_WAITQ_H */
