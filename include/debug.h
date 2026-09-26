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
#include <format.h>

/*
 * Both backends format with fmt_vformat (format.h: C argument rules, so %d/%u/%x
 * read the whole 32-bit value; no Exec call) and differ only in where each byte
 * goes:
 *   pistorm - magic address 0xdeadbeef, which Emu68/PiStorm traps and prints on
 *             the Pi console.  No Exec, no SysBase.
 *   serial  - Exec's RawPutChar (private LVO -516, the kprintf path), so
 *             anything that redirects it (Sashimi and the like) sees the output;
 *             otherwise it goes to the serial port at the system's baud rate.
 *             Debug printing has no context to carry a SysBase, so this sink —
 *             and only this one. Check-no-abs4 allowlists it; release builds have
 * 			   no serial sink.
 *
 * PrintPistorm is the shared formatter; some drivers (e.g. xhci) #define their
 * own tagged Kprintf on top of it, so it must exist for whichever backend is set.
 *
 * debug_putch is static inline rather than plain static: at the profile tier
 * Kprintf is a no-op, so a translation unit can include this header and never
 * reach the formatter — a plain static would then be an -Wunused-function.
 * Taking its address still forces an out-of-line copy where it is used.
 */
static inline void debug_putch(UBYTE data, APTR dummy)
{
	(void)dummy;
#ifdef DEBUG_SERIAL
	/* RawPutChar(d0) has no NDK prototype; call the LVO directly.  It may
	 * change d0/d1/a0/a1 (scratch) and needs a6 = SysBase. */
	register ULONG ch asm("d0") = data;
	asm volatile("move.l 4.w,%%a6\n\t"
	             "jsr -516(%%a6)"
	             : "+d"(ch)
	             :
	             : "d1", "a0", "a1", "a6", "cc", "memory");
#else
	*(volatile UBYTE *)0xdeadbeefUL = data;
#endif
}

static inline void PrintPistorm(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fmt_vformat(debug_putch, NULL, (CONST_STRPTR)fmt, args);
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
