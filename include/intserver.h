// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _INTSERVER_H
#define _INTSERVER_H

/*
 * EMU68_INTSERVER(name) - mark a function as an Exec interrupt server.
 *
 * A server answers its caller with the Z condition code, not with D0
 * (exec.library/AddIntServer): Z clear means "this interrupt was mine, stop walking the
 * chain", Z set means "not mine, keep going".  Nothing in C can state that, and m68k GCC
 * ends a function saving exactly one data register with `move.l (sp)+,dN`, which sets Z
 * from the RESTORED register instead of from the return value.  So it is checked from the
 * build instead -- emu68_isr_z_check().
 *
 * That check needs the server's exact extent in the LINKED module, because the bytes that
 * ship are the only ones worth checking.  AmigaOS HUNK carries no symbol table and most
 * servers are static, so the anchor is a section rather than a symbol: the linker map
 * lists an input section by name with its address and size whatever the symbol's linkage,
 * and a section attribute survives LTO (this is how .text.entry already works).  Hence one
 * section per server, named after it.  The name is repeated because the preprocessor
 * cannot read the declarator -- and that redundancy is itself checked, since the suffix is
 * compared against the SERVERS list passed to emu68_isr_z_check().
 *
 * used:    the server is only ever reached through is_Code, so nothing in the IR calls it.
 * noclone: stops partial inlining splitting a `foo.part.0` off into the same section,
 *          which would put a second function's rts inside the checked range.
 */
#define EMU68_INTSERVER(name) __attribute__((used, noclone, section(".text.isr." #name)))

#endif
