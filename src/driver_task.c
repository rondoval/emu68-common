// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * driver_task.c — spawn and join a self-contained driver worker task.
 * See driver_task.h for the readiness/liveness contract.
 *
 * No file-scope mutable state: safe to link into ROM-resident drivers.
 */

#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#include <clib/timer_protos.h>
#else
#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)
#include <proto/exec.h>
#include <proto/timer.h>
#endif

#include <dos/dos.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <devices/timer.h>

#include <driver_task.h>
#include <drv_timer.h>
#include <errors.h>
#include <minlist.h>
#include <debug.h>

s32 drv_task_spawn(APTR ctx, APTR entry, const char *name,
                   ULONG stackBytes, BYTE pri)
{
    KprintfT("[drv] %s: starting %s\n", __func__, name);

    struct MemList *ml = AllocMem(sizeof(struct MemList) + sizeof(struct MemEntry),
                                  MEMF_PUBLIC | MEMF_CLEAR);
    struct Task *task = AllocMem(sizeof(struct Task), MEMF_PUBLIC | MEMF_CLEAR);
    ULONG *stack = AllocMem(stackBytes, MEMF_PUBLIC | MEMF_CLEAR);

    if (ml == NULL || task == NULL || stack == NULL)
    {
        Kprintf("[drv] %s: alloc failed for %s\n", __func__, name);
        if (ml)
            FreeMem(ml, sizeof(struct MemList) + sizeof(struct MemEntry));
        if (task)
            FreeMem(task, sizeof(struct Task));
        if (stack)
            FreeMem(stack, stackBytes);
        return -ENOMEM;
    }

    /* Task, stack and MemList travel on the task's own tc_MemEntry, so Exec
     * frees all three when the task exits — the entry just returns. */
    ml->ml_NumEntries = 2;
    ml->ml_ME[0].me_Un.meu_Addr = task;
    ml->ml_ME[0].me_Length = sizeof(struct Task);
    ml->ml_ME[1].me_Un.meu_Addr = stack;
    ml->ml_ME[1].me_Length = stackBytes;

    task->tc_SPLower = stack;
    task->tc_SPUpper = &stack[stackBytes / sizeof(ULONG)];

    /* Push the entry's arguments (ctx, parent) onto the initial stack. */
    ULONG *sp = (ULONG *)task->tc_SPUpper;
    *--sp = (ULONG)FindTask(NULL);
    *--sp = (ULONG)ctx;
    task->tc_SPReg = sp;

    task->tc_Node.ln_Name = (char *)name;
    task->tc_Node.ln_Type = NT_TASK;
    task->tc_Node.ln_Pri = pri;

    _NewMinList((struct MinList *)&task->tc_MemEntry);
    AddHead(&task->tc_MemEntry, &ml->ml_Node);

    SetSignal(0UL, SIGBREAKF_CTRL_F | SIGBREAKF_CTRL_C);

    if (AddTask(task, entry, NULL) == NULL)
    {
        Kprintf("[drv] %s: AddTask(%s) failed\n", __func__, name);
        FreeMem(ml, sizeof(struct MemList) + sizeof(struct MemEntry));
        FreeMem(task, sizeof(struct Task));
        FreeMem(stack, stackBytes);
        return -ENOMEM;
    }

    /* The entry answers CTRL_F once it is in its wait loop, CTRL_C if it could
     * not get there. On failure its memory is already reclaimed via tc_MemEntry,
     * so there is nothing to free here. */
    ULONG sig = Wait(SIGBREAKF_CTRL_F | SIGBREAKF_CTRL_C);
    if (sig & SIGBREAKF_CTRL_C)
    {
        Kprintf("[drv] %s: %s failed to initialise\n", __func__, name);
        return -EIO;
    }

    KprintfT("[drv] %s: %s started\n", __func__, name);
    return 0;
}

void drv_task_join(struct Task **slot)
{
    if (slot == NULL || *slot == NULL)
        return;

    KprintfT("[drv] %s: stopping task=%lx\n", __func__, (ULONG)*slot);

    /* The task clears *slot on its way out, so the join is a poll. A timer paces
     * it; without one the poll still terminates, just hot. */
    struct drv_timer pacer;
    BOOL haveTimer = drv_timer_open(&pacer);

    Signal(*slot, SIGBREAKF_CTRL_C);
    while (*slot != NULL)
    {
        if (haveTimer)
            drv_timer_sleep_ms(&pacer, 250);
    }

    SetSignal(0UL, SIGBREAKF_CTRL_F | SIGBREAKF_CTRL_C);

    drv_timer_close(&pacer);

    KprintfT("[drv] %s: task stopped\n", __func__);
}

BYTE drv_unit_msgport_init(struct Unit *unit)
{
    _NewMinList((struct MinList *)&unit->unit_MsgPort.mp_MsgList);
    unit->unit_MsgPort.mp_SigTask = FindTask(NULL);

    BYTE sigbit = AllocSignal(-1);
    if (sigbit == -1)
        return -1;

    unit->unit_MsgPort.mp_SigBit = (UBYTE)sigbit;
    unit->unit_MsgPort.mp_Flags = PA_SIGNAL;
    unit->unit_MsgPort.mp_Node.ln_Type = NT_MSGPORT;
    return sigbit;
}

void drv_task_exit(struct Task **slot, struct Task *parent, BOOL ranLoop)
{
    *slot = NULL;
    Signal(parent, ranLoop ? SIGBREAKF_CTRL_F : SIGBREAKF_CTRL_C);
}
