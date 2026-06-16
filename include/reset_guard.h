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
 * Install once at device init; reset_guard_remove() is for expunge only
 * and fails if the ColdReboot vector was re-patched by someone else — the
 * device must then stay resident.
 */

#include <exec/types.h>
#include <exec/ports.h>
#include <exec/io.h>
#include <exec/interrupts.h>

typedef void (*reset_guard_prepare_t)(APTR user);

struct reset_guard
{
    /* All fields are private to reset_guard.c. */
    reset_guard_prepare_t rg_Prepare;
    APTR rg_User;
    CONST_STRPTR rg_Name;

    struct MsgPort rg_Port;        /* PA_IGNORE reply sink: no signal, no task */
    struct IOStdReq rg_AddIO;      /* OpenDevice + ADD/REM reset handler */
    struct IOStdReq rg_DoneIO;     /* one-shot RESETHANDLERDONE, sent by the handler */
    struct Interrupt rg_Handler;   /* keyboard reset handler */
    UWORD *rg_Trampoline;          /* generated ColdReboot stub (MEMF_PUBLIC) */
    APTR rg_OldColdReboot;
    volatile UBYTE rg_Prepared;    /* prepare() has run */
    BOOL rg_DeviceOpen;
};

BOOL reset_guard_install(struct reset_guard *rg, reset_guard_prepare_t prepare,
                         APTR user, CONST_STRPTR name);
BOOL reset_guard_remove(struct reset_guard *rg);

#endif /* _RESET_GUARD_H */
