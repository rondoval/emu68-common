// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef __DEBUG_H
#define __DEBUG_H

#ifdef DEBUG
#include <stdarg.h>

#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#else
#ifndef EXEC_BASE_NAME
#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)
#endif
#include <proto/exec.h>
#endif

/*
 * Both backends format with RawDoFmt and differ only in where each byte goes:
 *   pistorm - magic address 0xdeadbeef, which Emu68/PiStorm traps and prints on
 *             the Pi console.
 *   serial  - debug.lib KPutChar -> console (serial port @ 9600 baud), the same
 *             serial path KPrintF uses.
 *
 * PrintPistorm is the shared formatter; some drivers (e.g. xhci) #define their
 * own tagged Kprintf on top of it, so it must exist for whichever backend is set.
 */
#ifdef DEBUG_SERIAL
#include <clib/debug_protos.h>
static void putch(UBYTE data asm("d0"), APTR dummy asm("a3"))
{
	(void)dummy;
	if (data != 0)
	{
		KPutChar(data);
	}
}
#else
static void putch(UBYTE data asm("d0"), APTR dummy asm("a3"))
{
	(void)dummy;
	if (data != 0)
	{
		*(UBYTE *)0xdeadbeef = data;
	}
}
#endif

static inline void PrintPistorm(char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
	RawDoFmt((CONST_STRPTR)fmt, args, (APTR)putch, NULL);
#pragma GCC diagnostic pop
	va_end(args);
}

#define Kprintf PrintPistorm

#ifdef DEBUG_HIGH
#define KprintfH PrintPistorm
#else
#define KprintfH(...)
#endif

#define KASSERT(cond, msg) do { if (!(cond)) KprintfH("[kassert] " msg "\n"); } while (0)

#else
/* Debug off: expand to a statement (not empty) so `if (x) Kprintf(...);` keeps a
 * body and doesn't trip -Wempty-body. */
#define Kprintf(...) ((void)0)
#define KprintfH(...) ((void)0)
#define KASSERT(cond, msg) ((void)0)
#endif

#endif
