/* SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+ */
/*
 * textfmt.c — the stack's one printf engine.
 *
 * C argument rules: every argument is one 32-bit cell after promotion, so %d,
 * %u and %x read the whole value and the h/l/z modifiers are accepted and
 * ignored.  (RawDoFmt's rule — 16-bit unless 'l' — is not followed: nothing
 * formats through RawDoFmt any more, and that rule only turned a bare %d into
 * the wrong half of its argument.)
 *
 *   %[-][0][width][.prec][h|l|z]type
 *     -       left-justify within width
 *     0       pad numbers with zeros (after the sign)
 *     prec    at most this many characters of a %s
 *     type    d i u x X c s p %   (%p: 8 zero-padded hex digits, no 0x;
 *                                  %s of NULL: "(null)")
 *
 * An unknown conversion is emitted literally and consumes no argument, so one
 * bad specifier cannot desynchronise the rest of the line.  No 64-bit, no
 * floating point.
 *
 * No Exec call and no writable data: callable from interrupts, ROM-safe.
 */

#include <exec/types.h>
#include <format.h>

/* Next 32-bit argument cell, from wherever the front end keeps them. */
typedef ULONG (*fmt_next_t)(APTR args);

static void fmt_emit(fmt_putch_t putch, APTR out, const char *s, ULONG len)
{
	for (ULONG i = 0; i < len; i++)
		putch((UBYTE)s[i], out);
}

static void fmt_pad(fmt_putch_t putch, APTR out, char pad, LONG count)
{
	while (count-- > 0)
		putch((UBYTE)pad, out);
}

/* Digits of @v in @radix into the end of @end; returns the first digit. */
static char *fmt_digits(char *end, ULONG v, ULONG radix, BOOL upper)
{
	const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	char *p = end;

	do
	{
		*--p = dig[v % radix];
		v /= radix;
	} while (v != 0);
	return p;
}

static void fmt_core(fmt_putch_t putch, APTR out, CONST_STRPTR format, fmt_next_t next, APTR args)
{
	const char *f = (const char *)format;

	while (*f != '\0')
	{
		if (*f != '%')
		{
			putch((UBYTE)*f++, out);
			continue;
		}
		f++;

		BOOL left = FALSE;
		BOOL zero = FALSE;
		while (*f == '-' || *f == '0')
		{
			if (*f == '-')
				left = TRUE;
			else
				zero = TRUE;
			f++;
		}
		LONG width = 0;
		while (*f >= '0' && *f <= '9')
			width = width * 10 + (*f++ - '0');
		LONG prec = -1;
		if (*f == '.')
		{
			f++;
			prec = 0;
			while (*f >= '0' && *f <= '9')
				prec = prec * 10 + (*f++ - '0');
		}
		while (*f == 'h' || *f == 'l' || *f == 'z') /* every cell is 32-bit */
			f++;

		char buf[12]; /* "-2147483648" */
		char *end = buf + sizeof(buf);
		const char *body = end;
		LONG len;
		BOOL negative = FALSE;
		char pad = (zero && !left) ? '0' : ' ';

		char conv = *f;
		if (conv != '\0')
			f++;

		switch (conv)
		{
		case 's':
		{
			const char *s = (const char *)next(args);
			if (s == NULL)
				s = "(null)";
			len = 0;
			while (s[len] != '\0' && (prec < 0 || len < prec))
				len++;
			body = s;
			pad = ' '; /* strings never zero-pad */
			break;
		}
		case 'c':
			buf[0] = (char)next(args);
			body = buf;
			len = 1;
			pad = ' ';
			break;
		case 'd':
		case 'i':
		{
			LONG v = (LONG)next(args);
			negative = v < 0;
			/* negate in unsigned space so LONG_MIN doesn't overflow */
			body = fmt_digits(end, negative ? 0UL - (ULONG)v : (ULONG)v, 10, FALSE);
			len = end - body;
			break;
		}
		case 'u':
			body = fmt_digits(end, next(args), 10, FALSE);
			len = end - body;
			break;
		case 'x':
		case 'X':
			body = fmt_digits(end, next(args), 16, conv == 'X');
			len = end - body;
			break;
		case 'p':
			/* fixed 8 digits: lines up in a dump, as post-mortem tooling reads pointers */
			body = fmt_digits(end, next(args), 16, FALSE);
			len = end - body;
			pad = '0';
			if (width < 8)
				width = 8;
			break;
		case '%':
			buf[0] = '%';
			body = buf;
			len = 1;
			pad = ' ';
			break;
		default: /* unknown or truncated: echo it, consume nothing */
			putch((UBYTE)'%', out);
			if (conv != '\0')
				putch((UBYTE)conv, out);
			continue;
		}

		LONG fill = width - len - (negative ? 1 : 0);
		if (!left && pad == ' ')
			fmt_pad(putch, out, ' ', fill);
		if (negative)
			putch((UBYTE)'-', out);
		if (!left && pad == '0')
			fmt_pad(putch, out, '0', fill); /* zeros go after the sign */
		fmt_emit(putch, out, body, (ULONG)len);
		if (left)
			fmt_pad(putch, out, ' ', fill);
	}
}

/* --- va_list front end -------------------------------------------------- */

/* The va_list lives in a struct so a pointer to it can cross the callback:
 * va_list may be an array type, which cannot simply be passed by address. */
struct fmt_va
{
	va_list ap;
};

static ULONG fmt_next_va(APTR args)
{
	return va_arg(((struct fmt_va *)args)->ap, ULONG);
}

void fmt_vformat(fmt_putch_t putch, APTR out, CONST_STRPTR fmt, va_list ap)
{
	struct fmt_va v;

	va_copy(v.ap, ap);
	fmt_core(putch, out, fmt, fmt_next_va, &v);
	va_end(v.ap);
}

/* --- array front end ---------------------------------------------------- */

struct fmt_vec
{
	const ULONG *args;
};

static ULONG fmt_next_vec(APTR args)
{
	return *((struct fmt_vec *)args)->args++;
}

void fmt_aformat(fmt_putch_t putch, APTR out, CONST_STRPTR fmt, const ULONG *args)
{
	struct fmt_vec v = {args};

	fmt_core(putch, out, fmt, fmt_next_vec, &v);
}

/* --- buffer front ends (C99 snprintf semantics) ------------------------- */

struct fmt_buf
{
	STRPTR cursor;
	ULONG room; /* chars still storable, the terminator's slot excluded */
	LONG length;
};

static void fmt_buf_putch(UBYTE c, APTR out)
{
	struct fmt_buf *b = out;

	b->length++;
	if (b->room > 0)
	{
		*b->cursor++ = c;
		b->room--;
	}
}

static void fmt_buf_init(struct fmt_buf *b, STRPTR buffer, ULONG bufsize)
{
	b->cursor = buffer;
	b->room = (buffer && bufsize) ? bufsize - 1 : 0;
	b->length = 0;
}

static LONG fmt_buf_done(struct fmt_buf *b, STRPTR buffer, ULONG bufsize)
{
	if (buffer && bufsize)
		*b->cursor = '\0';
	return b->length;
}

LONG _VSNPrintf(STRPTR buffer, ULONG bufsize, CONST_STRPTR fmt, va_list args)
{
	struct fmt_buf b;

	fmt_buf_init(&b, buffer, bufsize);
	if (fmt)
		fmt_vformat(fmt_buf_putch, &b, fmt, args);
	return fmt_buf_done(&b, buffer, bufsize);
}

LONG _SNPrintf(STRPTR buffer, ULONG bufsize, CONST_STRPTR fmt, ...)
{
	LONG length;
	va_list args;

	va_start(args, fmt);
	length = _VSNPrintf(buffer, bufsize, fmt, args);
	va_end(args);

	return length;
}

LONG _SNPrintfArgs(STRPTR buffer, ULONG bufsize, CONST_STRPTR fmt, const ULONG *args)
{
	struct fmt_buf b;

	fmt_buf_init(&b, buffer, bufsize);
	if (fmt)
		fmt_aformat(fmt_buf_putch, &b, fmt, args);
	return fmt_buf_done(&b, buffer, bufsize);
}
