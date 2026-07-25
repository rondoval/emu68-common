// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef __DEBUG_H
#define __DEBUG_H

/*
 * Debug output: one sink, one tier.  cmake/Emu68CommonDebug.cmake owns both.
 *
 *   DEBUG_SINK  a sink exists (backend pistorm or serial) -> PrintPistorm below.
 *               Not a tier: it gates the formatter, the tier macros gate the
 *               printers, which is how KprintfP can print where Kprintf cannot.
 *   PROFILE     timing probes + perf_report        -> KprintfP
 *   DEBUG       asserts and ordinary logging       -> Kprintf, KASSERT
 *   TRACE       verbose logging                    -> KprintfT
 *
 * The tiers are cumulative (trace implies debug implies profile), and backend
 * "off" defines none of them, so nothing at all is emitted.
 */
#ifdef DEBUG_SINK
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
 *
 * putch is static inline rather than plain static: at the profile tier Kprintf
 * is a no-op, so a translation unit can include this header and never reach the
 * formatter — a plain static would then be an -Wunused-function. Taking its
 * address for RawDoFmt still forces an out-of-line copy where it is used.
 */
#ifdef DEBUG_SERIAL
#include <clib/debug_protos.h>
static inline void putch(UBYTE data asm("d0"), APTR dummy asm("a3"))
{
	(void)dummy;
	if (data != 0)
	{
		KPutChar(data);
	}
}
#else
static inline void putch(UBYTE data asm("d0"), APTR dummy asm("a3"))
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

#endif /* DEBUG_SINK */

/*
 * One printer per tier. A disabled printer expands to a statement (not empty) so
 * `if (x) Kprintf(...);` keeps a body and doesn't trip -Wempty-body.
 */
#ifdef PROFILE
#define KprintfP PrintPistorm
#else
#define KprintfP(...) ((void)0)
#endif

#ifdef DEBUG
#define Kprintf PrintPistorm
#else
#define Kprintf(...) ((void)0)
#endif

#ifdef TRACE
#define KprintfT PrintPistorm
#else
#define KprintfT(...) ((void)0)
#endif

/* Asserts are debug-tier: a failed invariant is not verbose chatter. */
#ifdef DEBUG
#define KASSERT(cond, msg) do { if (!(cond)) Kprintf("[kassert] " msg "\n"); } while (0)
#else
#define KASSERT(cond, msg) ((void)0)
#endif

#endif
