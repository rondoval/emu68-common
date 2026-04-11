/* SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+ */
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#else
#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)
#include <proto/exec.h>
#endif

#include <exec/types.h>
#include <format.h>

struct compat_printf_state
{
	STRPTR cursor;
	ULONG remaining;
	LONG required;
};

static void compat_printf_putch(UBYTE data asm("d0"), struct compat_printf_state *state asm("a3"))
{
	state->required++;

	if (state->remaining > 0)
	{
		*state->cursor++ = data;
		state->remaining--;
	}
}

LONG _VSNPrintf(STRPTR buffer, ULONG bufsize, CONST_STRPTR fmt, va_list args)
{
	struct compat_printf_state state = {
		.cursor = buffer,
		.remaining = bufsize,
		.required = 0,
	};

	if (fmt)
		RawDoFmt(fmt, args, (APTR)compat_printf_putch, &state);

	if (buffer && bufsize)
	{
		if (state.remaining == 0)
			*(state.cursor - 1) = '\0';
		else
			*state.cursor = '\0';
	}

	return state.required;
}

LONG _SNPrintf(STRPTR buffer, ULONG bufsize, CONST_STRPTR fmt, ...)
{
	LONG required;
	va_list args;

	va_start(args, fmt);
	required = _VSNPrintf(buffer, bufsize, fmt, args);
	va_end(args);

	return required;
}