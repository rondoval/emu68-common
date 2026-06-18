// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
//
// Glue for the serial debug backend (debug.lib). Added to every target that links
// libdebug.a by emu68_debug_backend_finalize() when EMU68_DEBUG_BACKEND=serial.
//
// debug.lib ships as a single object (kdebug.o), so pulling in KPutChar also drags
// KGetNum, which references the compiler helper __divsi3.
__asm__(
	"	.text\n"
	"	.weak	___divsi3\n"
	"___divsi3:\n"
	"	divs.l	%d1,%d0\n"
	"	rts\n"
);
