// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * memory.c — the C halves of the freestanding C runtime memory primitives
 *            declared in memory.h.
 *
 * The drivers are built -ffreestanding -nostdlib, so no libc is linked.  Even in
 * freestanding mode the C standard lets GCC synthesise calls to memset/memcpy/
 * memmove/memcmp out of ordinary loops and aggregate (struct) init/assignment;
 * at -O3 it does so freely, so these four symbols must exist.
 *
 * Where each one lives:
 *   memset  — memset_movem.S: longword loop for short fills, movem.l blocks for
 *             long ones.
 *   memcpy  — memcpy_movem.S: the same shape, any alignment.
 *   memmove — here: memcpy for the non-overlapping / forward-safe case, a
 *             descending copy for the (compiler-rare) overlapping dst > src case.
 *   memcmp  — here: long-at-a-time compare with a byte tail.
 * None makes an Exec call or owns writable data: all four are callable from
 * interrupts and SANA-II copy hooks, and ROM-safe.
 *
 * This translation unit is compiled -fno-tree-loop-distribute-patterns
 * -fno-builtin (see CMakeLists.txt) so the copy/compare loops below are not
 * rewritten into self-referential calls (e.g. memmove() calling memmove()).
 *
 * This TU is compiled -fno-lto (see CMakeLists.txt), which puts it on the same footing as
 * its two siblings memcpy_movem.S and memset_movem.S: assembly is never LTO'd, so those
 * two are always real objects.  That is the whole reason it works.
 *
 * GCC synthesises these four calls during *ltrans* codegen -- after the IR compilation
 * phase has ended -- and ld can still satisfy such a late reference out of an archive,
 * but only when the member is a real object.  bsdsocket.library's map shows exactly that:
 * all three pulled by an ltrans object, not by anything in the IR.
 *
 *     libcommon.a(memcpy_movem.S.obj)   <- ...ltrans0.ltrans.o (memcpy)
 *     libcommon.a(memory.c.obj)         <- ...ltrans1.ltrans.o (memmove)
 *     libcommon.a(memset_movem.S.obj)   <- ...ltrans0.ltrans.o (memset)
 *
 * An IR member cannot be brought in and compiled at that point, so built as LTO IR this
 * TU leaves `undefined reference to memcmp/memmove' at the final link.  Tested both ways
 * on gcc 16.2, and __attribute__((used)) does NOT help: the problem is not that the body
 * is dropped, it is that the member can no longer be codegen'd.
 */

#include <types.h>
#include <memory.h> /* prototypes (with __SIZE_TYPE__) */

/* ----------------------------------------------------------------------------
 * memmove — memcpy (memcpy_movem.S) is not overlap-safe, so memmove guards and
 * falls back to a descending copy only for the overlapping dst > src case
 * (which the compiler essentially never emits).
 * ------------------------------------------------------------------------- */

void *memmove(void *dst, const void *src, __SIZE_TYPE__ n)
{
	UBYTE *d = (UBYTE *)dst;
	const UBYTE *s = (const UBYTE *)src;
	ULONG cnt = (ULONG)n;

	if (d == s || cnt == 0)
		return dst;

	/* No overlap, or dst below src: a forward copy is safe. */
	if (d < s || d >= s + cnt)
		return memcpy(dst, src, cnt);

	/* Overlapping with dst > src: copy descending so we never clobber unread
	 * source bytes.  Long-at-a-time once both end pointers are long-aligned. */
	d += cnt;
	s += cnt;

	while (cnt && (((ULONG)d | (ULONG)s) & 3))
	{
		*--d = *--s;
		cnt--;
	}

	{
		ULONG *dl = (ULONG *)d;
		const ULONG *sl = (const ULONG *)s;

		while (cnt >= sizeof(ULONG))
		{
			*--dl = *--sl;
			cnt -= sizeof(ULONG);
		}

		d = (UBYTE *)dl;
		s = (const UBYTE *)sl;
	}

	while (cnt)
	{
		*--d = *--s;
		cnt--;
	}

	return dst;
}

/* ----------------------------------------------------------------------------
 * memcmp — long-at-a-time when both inputs share long alignment, byte tail
 * locates the exact differing byte and supplies the signed result.
 * ------------------------------------------------------------------------- */

int memcmp(const void *s1, const void *s2, __SIZE_TYPE__ n)
{
	const UBYTE *a = (const UBYTE *)s1;
	const UBYTE *b = (const UBYTE *)s2;
	ULONG cnt = (ULONG)n;

	if ((((ULONG)a | (ULONG)b) & 3) == 0)
	{
		const ULONG *la = (const ULONG *)a;
		const ULONG *lb = (const ULONG *)b;

		while (cnt >= sizeof(ULONG) && *la == *lb)
		{
			la++;
			lb++;
			cnt -= sizeof(ULONG);
		}

		a = (const UBYTE *)la;
		b = (const UBYTE *)lb;
	}

	while (cnt)
	{
		if (*a != *b)
			return (int)*a - (int)*b;
		a++;
		b++;
		cnt--;
	}

	return 0;
}
