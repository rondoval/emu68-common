// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _FORMAT_H
#define _FORMAT_H

#include <stdarg.h>
#include <types.h>

/*
 * The stack's printf engine (textfmt.c). Every argument is one 32-bit cell,
 * so %d/%u/%x need no 'l' (h/l/z are accepted and ignored).
 * %[-][0][width][.prec]{d i u x X c s p %}; no 64-bit, no floating point.
 * No Exec call and no writable data: interrupt-safe and ROM-safe.
 */

/* Output callback: receives each character; no terminating NUL. */
typedef void (*fmt_putch_t)(UBYTE c, APTR out);

/* Streaming front ends: arguments from a va_list, or from an array of 32-bit
 * cells (the AmigaOS vsyslog/VPrintf calling convention). */
void fmt_vformat(fmt_putch_t putch, APTR out, CONST_STRPTR fmt, va_list ap);
void fmt_aformat(fmt_putch_t putch, APTR out, CONST_STRPTR fmt, const ULONG *args);

/* Buffer front ends, C99 snprintf semantics: at most bufsize-1 characters plus
 * a NUL (when bufsize > 0); the return value is the length the whole string
 * would have, without the NUL, so a result >= bufsize means it was truncated. */
LONG _VSNPrintf(STRPTR buffer, ULONG bufsize, CONST_STRPTR fmt, va_list args);
LONG _SNPrintf(STRPTR buffer, ULONG bufsize, CONST_STRPTR fmt, ...);
LONG _SNPrintfArgs(STRPTR buffer, ULONG bufsize, CONST_STRPTR fmt, const ULONG *args);

#endif
