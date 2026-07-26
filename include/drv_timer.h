// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * A timer.device (MICROHZ) instance held open across a burst of waits: one
 * open serves many synchronous sleeps (DoIO) or periodic arms (SendIO +
 * consume), replacing the CreateMsgPort/OpenDevice/CloseDevice dance per
 * wait.  All state is caller-owned — ROM-safe.
 *
 * Users: the xhci root-hub port waits, the three drivers' unit-task tick
 * timers, and drv_task_join's pacing poll.
 */

#ifndef _DRV_TIMER_H
#define _DRV_TIMER_H

#include <exec/types.h>
#include <exec/ports.h>

struct timerequest;

struct drv_timer
{
	struct MsgPort *port;
	struct timerequest *req;
};

/* Open a MICROHZ timer; FALSE = timer.device unavailable (fields left NULL,
 * every other call is then a safe no-op). */
BOOL drv_timer_open(struct drv_timer *t);
void drv_timer_close(struct drv_timer *t);

/* Synchronous sleep (DoIO). Must not be mixed with an armed request. */
void drv_timer_sleep_ms(struct drv_timer *t, ULONG ms);

/* Periodic use: arm asynchronously, Wait() on drv_timer_sigmask, consume the
 * completed request, re-arm.  Cancel aborts+consumes an armed request. */
void drv_timer_arm_ms(struct drv_timer *t, ULONG ms);
void drv_timer_consume(struct drv_timer *t);
void drv_timer_cancel(struct drv_timer *t);

static inline ULONG drv_timer_sigmask(const struct drv_timer *t)
{
	return t->port ? (1UL << t->port->mp_SigBit) : 0UL;
}

#endif /* _DRV_TIMER_H */
