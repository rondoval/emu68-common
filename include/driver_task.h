// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _DRIVER_TASK_H
#define _DRIVER_TASK_H

#include <types.h>

struct Task;

/*
 * A driver worker task's lifecycle, shared by the storage and network drivers:
 * allocate a self-contained task, launch it, and join it.
 *
 * The entry function is the task body. It receives its context pointer and the
 * parent task to notify, and it owns its task-pointer slot: it publishes
 * FindTask(NULL) into the slot before it reports readiness and clears the slot
 * as the last thing it does. drv_task_join() waits on that clear, so the slot
 * is the sole liveness handshake.
 *
 * Readiness is reported back to the parent with a signal, which drv_task_spawn()
 * blocks on:
 *   SIGBREAKF_CTRL_F  the task reached its wait loop     -> spawn returns 0
 *   SIGBREAKF_CTRL_C  init failed, the task has exited    -> spawn returns -EIO
 *
 * The entry signature is void entry(APTR ctx, struct Task *parent); it is passed
 * as APTR, exactly as to AddTask().
 */

/*
 * Allocate a Task, its stack and a MemList (all carried on the task's own
 * tc_MemEntry, so Exec reclaims them when the task exits), push (ctx, parent)
 * as the entry's arguments, AddTask it, and block until the entry reports
 * success or failure.
 *
 * Returns 0 on success, -ENOMEM if the allocations or AddTask fail (nothing is
 * launched), -EIO if the task started but its initialisation failed (its memory
 * is already reclaimed by then).
 */
s32 drv_task_spawn(APTR ctx, APTR entry, const char *name,
                   ULONG stackBytes, BYTE pri);

/*
 * Signal SIGBREAKF_CTRL_C to *slot and wait for the entry to clear it, paced by
 * a 250 ms timer.device poll (a busy-poll if timer.device will not open). A NULL
 * or already-clear slot returns at once.
 */
void drv_task_join(struct Task **slot);

#endif
