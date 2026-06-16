// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * reset_guard — pre-reset quiesce hooks for DMA-capable drivers.
 *
 * ROM-able (no writable module state) and task-less; see reset_guard.h for
 * the model and the prepare() contract, and the driver-stack
 * README-internal.md for the reset-path analysis behind it.
 */
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
extern struct ExecBase *SysBase;
#define EXEC_BASE_NAME SysBase
#else
#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)
#include <proto/exec.h>
#endif

#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/nodes.h>
#include <devices/keyboard.h>

#include <reset_guard.h>
#include <memory.h>
#include <timing.h>
#include <debug.h>

#define LVO_COLDREBOOT (-726)

/* Generated ColdReboot stub: movea.l #rg,a1 ; jmp entry.l */
#define TRAMP_WORDS  6
#define TRAMP_BYTES  (TRAMP_WORDS * sizeof(UWORD))

static void reset_guard_run_prepare(struct reset_guard *rg)
{
	Disable();
	UBYTE already = rg->rg_Prepared;
	rg->rg_Prepared = 1;
	Enable();

	if (!already && rg->rg_Prepare)
		rg->rg_Prepare(rg->rg_User);
}

/* Target of the generated trampoline, which loads the guard into A1.
 * Runs prepare, then chains to the previous vector — never returns. */
static void reset_guard_coldreboot_entry(struct reset_guard *rg asm("a1"))
{
	reset_guard_run_prepare(rg);

	APTR fn = rg->rg_OldColdReboot;
	struct ExecBase *sb = EXEC_BASE_NAME;
	asm volatile("move.l %0,%%a0\n\t"
	             "move.l %1,%%a6\n\t"
	             "jmp (%%a0)"
	             :
	             : "g"(fn), "g"(sb)
	             : "a0", "a6");
}

/* Keyboard reset handler: keyboard.device calls is_Code with is_Data in A1,
 * potentially in interrupt context — prepare() is interrupt-safe by
 * contract, and DONE is a SendIO whose reply lands on the PA_IGNORE port. */
static void reset_guard_kbd_handler(struct reset_guard *rg asm("a1"))
{
	reset_guard_run_prepare(rg);
	SendIO((struct IORequest *)&rg->rg_DoneIO);
}

/* Submit a keyboard.device command on the signal-less port and busy-poll
 * for completion (DoIO/WaitIO need a signal).  Install/expunge context
 * only; both commands are documented to complete immediately. */
static BYTE reset_guard_do_command(struct reset_guard *rg, UWORD command)
{
	struct IOStdReq *io = &rg->rg_AddIO;

	io->io_Command = command;
	io->io_Data = &rg->rg_Handler;
	SendIO((struct IORequest *)io);
	while (!CheckIO((struct IORequest *)io))
		delay_us(100);

	/* A non-quick completion was ReplyMsg'd onto the PA_IGNORE port;
	 * detach it so the request can be reused. */
	Disable();
	if (io->io_Message.mn_Node.ln_Type == NT_REPLYMSG)
		Remove(&io->io_Message.mn_Node);
	Enable();

	return io->io_Error;
}

static APTR lvo_target(LONG lvo)
{
	/* LVO entries are JMP abs.l: 0x4EF9 opcode word + 32-bit address. */
	return *(APTR *)((UBYTE *)EXEC_BASE_NAME + lvo + 2);
}

BOOL reset_guard_install(struct reset_guard *rg, reset_guard_prepare_t prepare,
                         APTR user, CONST_STRPTR name)
{
	mem_zero(rg, sizeof(*rg));
	rg->rg_Prepare = prepare;
	rg->rg_User = user;
	rg->rg_Name = name;

	/* PA_IGNORE reply sink: replies just queue, nothing is signalled. */
	rg->rg_Port.mp_Node.ln_Type = NT_MSGPORT;
	rg->rg_Port.mp_Node.ln_Name = (char *)name;
	rg->rg_Port.mp_Flags = PA_IGNORE;
	rg->rg_Port.mp_MsgList.lh_Head = (struct Node *)&rg->rg_Port.mp_MsgList.lh_Tail;
	rg->rg_Port.mp_MsgList.lh_Tail = NULL;
	rg->rg_Port.mp_MsgList.lh_TailPred = (struct Node *)&rg->rg_Port.mp_MsgList.lh_Head;

	rg->rg_AddIO.io_Message.mn_ReplyPort = &rg->rg_Port;
	rg->rg_AddIO.io_Message.mn_Length = sizeof(struct IOStdReq);
	if (OpenDevice((CONST_STRPTR)"keyboard.device", 0,
	               (struct IORequest *)&rg->rg_AddIO, 0) != 0)
	{
		Kprintf("[%s] reset guard: OpenDevice(keyboard.device) failed\n", name);
		return FALSE;
	}
	rg->rg_DeviceOpen = TRUE;

	rg->rg_Handler.is_Node.ln_Type = NT_INTERRUPT;
	rg->rg_Handler.is_Node.ln_Pri = 0;
	rg->rg_Handler.is_Node.ln_Name = (char *)name;
	rg->rg_Handler.is_Data = rg;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
	rg->rg_Handler.is_Code = (VOID (*)())reset_guard_kbd_handler;
#pragma GCC diagnostic pop

	if (reset_guard_do_command(rg, KBD_ADDRESETHANDLER) != 0)
	{
		Kprintf("[%s] reset guard: KBD_ADDRESETHANDLER failed\n", name);
		CloseDevice((struct IORequest *)&rg->rg_AddIO);
		rg->rg_DeviceOpen = FALSE;
		return FALSE;
	}

	/* Pre-initialized one-shot DONE request; the handler only SendIO()s. */
	rg->rg_DoneIO.io_Message.mn_ReplyPort = &rg->rg_Port;
	rg->rg_DoneIO.io_Message.mn_Length = sizeof(struct IOStdReq);
	rg->rg_DoneIO.io_Device = rg->rg_AddIO.io_Device;
	rg->rg_DoneIO.io_Unit = rg->rg_AddIO.io_Unit;
	rg->rg_DoneIO.io_Command = KBD_RESETHANDLERDONE;
	rg->rg_DoneIO.io_Data = &rg->rg_Handler;

	/* The ColdReboot stub has no argument path, so generate a trampoline
	 * that carries the guard pointer: movea.l #rg,a1 ; jmp entry.l */
	UWORD *t = AllocMem(TRAMP_BYTES, MEMF_PUBLIC);
	if (!t)
	{
		reset_guard_do_command(rg, KBD_REMRESETHANDLER);
		CloseDevice((struct IORequest *)&rg->rg_AddIO);
		rg->rg_DeviceOpen = FALSE;
		return FALSE;
	}
	t[0] = 0x227C;
	t[1] = (UWORD)((ULONG)rg >> 16);
	t[2] = (UWORD)((ULONG)rg & 0xFFFFUL);
	t[3] = 0x4EF9;
	t[4] = (UWORD)((ULONG)reset_guard_coldreboot_entry >> 16);
	t[5] = (UWORD)((ULONG)reset_guard_coldreboot_entry & 0xFFFFUL);
	CacheClearU(); /* the trampoline is about to be fetched as code */

	rg->rg_Trampoline = t;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
	rg->rg_OldColdReboot = (APTR)SetFunction((struct Library *)EXEC_BASE_NAME,
	                                         LVO_COLDREBOOT, (ULONG (*)())t);
#pragma GCC diagnostic pop

	Kprintf("[%s] reset guard installed\n", name);
	return TRUE;
}

BOOL reset_guard_remove(struct reset_guard *rg)
{
	if (!rg->rg_DeviceOpen)
		return TRUE;

	if (rg->rg_Trampoline)
	{
		/* If something patched ColdReboot after us, the chain runs
		 * through our trampoline and must stay resident. */
		if (lvo_target(LVO_COLDREBOOT) != (APTR)rg->rg_Trampoline)
			return FALSE;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
		SetFunction((struct Library *)EXEC_BASE_NAME, LVO_COLDREBOOT,
		            (ULONG (*)())rg->rg_OldColdReboot);
#pragma GCC diagnostic pop
		FreeMem(rg->rg_Trampoline, TRAMP_BYTES);
		rg->rg_Trampoline = NULL;
	}

	reset_guard_do_command(rg, KBD_REMRESETHANDLER);
	CloseDevice((struct IORequest *)&rg->rg_AddIO);
	rg->rg_DeviceOpen = FALSE;
	return TRUE;
}
