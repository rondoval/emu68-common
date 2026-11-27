// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _RESET_GUARD_H
#define _RESET_GUARD_H

/*
 * reset_guard — run a driver "prepare for reset" callback before the Amiga
 * resets, on both reset flavors that exist on PiStorm/Emu68:
 *
 *  - Ctrl-Amiga-Amiga: the keyboard reset-warning protocol runs the
 *    keyboard.device reset-handler chain while the keyboard MCU waits;
 *    after KBD_RESETHANDLERDONE the MCU hard-resets the machine (a full
 *    Pi reboot under Emu68).  The guard's handler runs prepare() inline
 *    and only then reports DONE.
 *
 *  - ColdReboot() (C:Reboot, Installer, ...): a SetFunction() trampoline
 *    runs prepare() in the rebooting caller's context, then chains to the
 *    previous vector.  Trampolines from several drivers chain in install
 *    order.
 *
 * ROM-able and task-less: the module keeps no writable static state (the
 * ColdReboot trampoline is generated into allocated memory and carries the
 * guard pointer), spawns no tasks, and all mutable state lives in the
 * caller-provided struct (typically embedded in the device base).
 *
 * prepare() contract — INTERRUPT-SAFE:
 *  - MMIO, busy-wait delays (timing.h), Disable()/Enable(), and
 *    interrupt-callable exec primitives only; no Wait(), no allocation,
 *    no DOS
 *  - bounded: the keyboard grace period (~10 s) is shared by all drivers;
 *    keep the per-driver worst case <= 5 s
 *  - terminal: the hardware stays down until the next boot re-probes it
 *  - safe against not-yet-probed or already-quiesced controllers
 * prepare() runs at most once per session, even though the Ctrl-A-A path
 * passes through both hooks.
 *
 * Deferred keyboard path (reset_guard_set_defer) — for drivers that
 * filesystems sit on.  A filesystem's own reset handler only signals its
 * task, which then writes its pending data and reports done (the RKM's model;
 * PFS3 does this at priority -32), so a block driver that shuts down inside
 * the handler cuts that flush off: the writes never complete, the filesystem
 * never reports done, and the keyboard waits out its 10 s.  With a defer()
 * callback set, the keyboard handler calls defer(user) instead of prepare()
 * and reports nothing; the driver keeps serving I/O, quiesces from one of its
 * own tasks once the filesystems are done, and then calls reset_guard_done().
 * It must do so well inside the keyboard's 10 s.  defer() runs in interrupt
 * context (set state, Signal()); the ColdReboot path is not affected.
 *
 * Ctrl-Amiga-Amiga always restarts the whole Pi (the housekeeper's
 * watchdog reset), VPU and PCIe included, so on that path nothing a driver
 * stops matters to the next session: only data still on its way to a drive
 * does.  A driver whose prepare() only stops DMA or a link may report done
 * at once there: a defer() that calls reset_guard_done().  The ColdReboot
 * path restarts the 68k alone; there the quiesce is what protects RAM.
 *
 * keyboard.device runs its handlers in priority order, and allows only
 * software-interrupt priorities for them (-32, -16, 0, 16, 32; RKM Devices,
 * Keyboard).  The guard's handler is at 0; PFS3's, for one, at -32.
 *
 * Install once at device init; reset_guard_remove() is for expunge only
 * and fails if the ColdReboot vector was re-patched by someone else — the
 * device must then stay resident.
 */

#include <exec/types.h>
#include <exec/ports.h>
#include <exec/io.h>
#include <exec/interrupts.h>

typedef void (*reset_guard_prepare_t)(APTR user);
typedef void (*reset_guard_defer_t)(APTR user);

struct reset_guard
{
    /* All fields are private to reset_guard.c. */
    struct ExecBase *rg_SysBase;   /* the installer's; every path, incl. ColdReboot, uses it */
    reset_guard_prepare_t rg_Prepare; /* the driver's quiesce, run inline */
    reset_guard_defer_t rg_Defer;  /* keyboard path: called instead of prepare(); NULL = inline */
    APTR rg_User;                  /* argument of prepare() and defer() */
    CONST_STRPTR rg_Name;          /* the driver's name, for the port, the handler and logs */

    struct MsgPort rg_Port;        /* PA_IGNORE reply sink: no signal, no task */
    struct IOStdReq rg_AddIO;      /* OpenDevice + ADD/REM reset handler */
    struct IOStdReq rg_DoneIO;     /* one-shot RESETHANDLERDONE, sent by the handler */
    struct Interrupt rg_Handler;   /* keyboard reset handler */
    UWORD *rg_Trampoline;          /* generated ColdReboot stub (MEMF_PUBLIC) */
    APTR rg_OldColdReboot;
    volatile UBYTE rg_Prepared;    /* prepare() has run, or a deferred driver has quiesced */
    volatile UBYTE rg_DoneSent;    /* KBD_RESETHANDLERDONE has been sent */
    BOOL rg_DeviceOpen;            /* keyboard.device is open on rg_AddIO */
};

BOOL reset_guard_install(struct reset_guard *rg, struct ExecBase *SysBase, reset_guard_prepare_t prepare,
                         APTR user, CONST_STRPTR name);
BOOL reset_guard_remove(struct reset_guard *rg);

/*
 * reset_guard_set_defer - defer the keyboard path to the driver (see above).
 * Call after reset_guard_install(); a warning that arrives before it takes
 * the inline path.
 */
void reset_guard_set_defer(struct reset_guard *rg, reset_guard_defer_t defer);

/*
 * reset_guard_done - report a deferred keyboard reset as done.
 * Sends KBD_RESETHANDLERDONE once, and marks prepare() as run, so a
 * ColdReboot() afterwards does not repeat it.  Task or interrupt context.
 */
void reset_guard_done(struct reset_guard *rg);

/*
 * reset_guard_can_wait - TRUE when the calling context may Wait(): a task,
 * not an interrupt or exception (supervisor mode), and outside Disable().
 * For a deferred driver's prepare(), which then runs on the ColdReboot path
 * only: C:Reboot calls from an ordinary task, where the driver can do work
 * that needs its own tasks, but ColdReboot() may come from anywhere.
 */
BOOL reset_guard_can_wait(struct reset_guard *rg);

#endif /* _RESET_GUARD_H */
