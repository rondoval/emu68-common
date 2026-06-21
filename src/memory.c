// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * memory.c — the freestanding C runtime memory primitives (implementation of
 *            the four symbols declared in memory.h).
 *
 * The drivers are built -ffreestanding -nostdlib, so no libc is linked.  Even in
 * freestanding mode the C standard lets GCC synthesise calls to memset/memcpy/
 * memmove/memcmp out of ordinary loops and aggregate (struct) init/assignment;
 * at -O3 it does so freely, so these four symbols must exist.
 *
 * Strategy — reuse the platform's optimised routines, hand-roll only what Exec
 * does not provide:
 *   memset  — asm-optimised byte fill (single primitive; the old mem_zero family
 *             generalised so a fill value of 0 costs exactly what zeroing did and
 *             any other byte comes for free).  Exec has no fill function.
 *   memcpy  — thin wrapper over Exec CopyMem (movem/move16, any alignment/size).
 *             Note CopyMem's argument order is (source, dest), the reverse of C.
 *   memmove — CopyMem for the non-overlapping / forward-safe case; an in-house
 *             descending copy for the (compiler-rare) overlapping dst > src case,
 *             because CopyMem is not overlap-safe.
 *   memcmp  — long-at-a-time compare with a byte tail.  Exec has no equivalent.
 *
 * This translation unit is compiled -fno-tree-loop-distribute-patterns
 * -fno-builtin (see CMakeLists.txt) so the fill/copy/compare loops below are not
 * rewritten into self-referential calls (e.g. memcpy() calling memcpy()).
 */

#include <types.h>
#include <memory.h> /* prototypes (with __SIZE_TYPE__) + EXEC_BASE_NAME for CopyMem */

/* ----------------------------------------------------------------------------
 * memset — asm-optimised byte fill.
 *
 * Size buckets (same thresholds the zero-only predecessor used): up to 63 bytes
 * a single-long store loop, up to 511 bytes a 4x-unrolled loop, beyond that the
 * movem block filler.  Every "store" uses the broadcast fill value.
 * ------------------------------------------------------------------------- */

#define MEM_FILL_1_MAX 63UL
#define MEM_FILL_4_MAX 511UL

/* end = one-past-the-last long; fills downward.  Defined in memset_movem.S. */
void mem_fill_asm_movem_impl(ULONG *end, ULONG blocks, ULONG value);

static ULONG *mem_fill_align_long(APTR dst, ULONG *len, ULONG value)
{
	ULONG addr = (ULONG)dst;

	if (*len && (addr & 1))
	{
		*(UBYTE *)addr = (UBYTE)value;
		addr += sizeof(UBYTE);
		*len -= sizeof(UBYTE);
	}

	if (*len >= sizeof(UWORD) && (addr & 2))
	{
		*(UWORD *)addr = (UWORD)value;
		addr += sizeof(UWORD);
		*len -= sizeof(UWORD);
	}

	return (ULONG *)addr;
}

static void mem_fill_tail(ULONG *dst, ULONG len, ULONG value)
{
	ULONG addr = (ULONG)dst;

	if (len >= sizeof(UWORD))
	{
		*(UWORD *)addr = (UWORD)value;
		addr += sizeof(UWORD);
		len -= sizeof(UWORD);
	}

	if (len)
		*(UBYTE *)addr = (UBYTE)value;
}

static void mem_fill_asm_1(APTR dst, ULONG len, ULONG value)
{
	ULONG *d32 = mem_fill_align_long(dst, &len, value);
	ULONG long_count = len / sizeof(ULONG);
	ULONG tail = len & (sizeof(ULONG) - 1);

	if (long_count)
	{
		asm volatile(
			"move.l %[count], %%d0\n\t"
			"1:\n\t"
			"move.l %[val], (%[dst])+\n\t"
			"subq.l #1, %%d0\n\t"
			"bne.s 1b\n\t"
			: [dst] "+a"(d32)
			: [count] "r"(long_count), [val] "r"(value)
			: "d0", "cc", "memory");
	}

	mem_fill_tail(d32, tail, value);
}

static void mem_fill_asm_4(APTR dst, ULONG len, ULONG value)
{
	ULONG *d32 = mem_fill_align_long(dst, &len, value);
	ULONG long_count = len / sizeof(ULONG);
	ULONG quads = long_count / 4;
	ULONG rem = long_count & 3;
	ULONG tail = len & (sizeof(ULONG) - 1);

	if (long_count)
	{
		asm volatile(
			"move.l %[quads], %%d0\n\t"
			"beq.s 2f\n\t"
			"1:\n\t"
			"move.l %[val], (%[dst])+\n\t"
			"move.l %[val], (%[dst])+\n\t"
			"move.l %[val], (%[dst])+\n\t"
			"move.l %[val], (%[dst])+\n\t"
			"subq.l #1, %%d0\n\t"
			"bne.s 1b\n\t"
			"2:\n\t"
			"move.l %[rem], %%d0\n\t"
			"beq.s 4f\n\t"
			"3:\n\t"
			"move.l %[val], (%[dst])+\n\t"
			"subq.l #1, %%d0\n\t"
			"bne.s 3b\n\t"
			"4:\n\t"
			: [dst] "+a"(d32)
			: [quads] "r"(quads), [rem] "r"(rem), [val] "r"(value)
			: "d0", "cc", "memory");
	}

	mem_fill_tail(d32, tail, value);
}

static void mem_fill_asm_movem(APTR dst, ULONG len, ULONG value)
{
	ULONG *d32 = mem_fill_align_long(dst, &len, value);
	ULONG long_count = len / sizeof(ULONG);
	ULONG blocks = long_count / 16;
	ULONG rem = long_count % 16;
	ULONG tail = len & (sizeof(ULONG) - 1);

	if (blocks)
	{
		mem_fill_asm_movem_impl(d32 + (blocks * 16), blocks, value);
		d32 += blocks * 16;
	}

	while (rem)
	{
		*d32++ = value;
		rem--;
	}

	mem_fill_tail(d32, tail, value);
}

void *memset(void *dst, int c, __SIZE_TYPE__ n)
{
	ULONG len = (ULONG)n;
	ULONG value = (UBYTE)c;

	value |= value << 8;
	value |= value << 16;

	if (len <= MEM_FILL_1_MAX)
		mem_fill_asm_1(dst, len, value);
	else if (len <= MEM_FILL_4_MAX)
		mem_fill_asm_4(dst, len, value);
	else
		mem_fill_asm_movem(dst, len, value);

	return dst;
}

/* ----------------------------------------------------------------------------
 * memcpy / memmove — Exec CopyMem (source, dest, size).  CopyMem is not
 * overlap-safe, so memmove guards and falls back to a descending copy only for
 * the overlapping dst > src case (which the compiler essentially never emits).
 * ------------------------------------------------------------------------- */

void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n)
{
	CopyMem(src, dst, (ULONG)n); /* NB: CopyMem args are (source, dest) */
	return dst;
}

void *memmove(void *dst, const void *src, __SIZE_TYPE__ n)
{
	UBYTE *d = (UBYTE *)dst;
	const UBYTE *s = (const UBYTE *)src;
	ULONG cnt = (ULONG)n;

	if (d == s || cnt == 0)
		return dst;

	/* No overlap, or dst below src: a forward copy is safe — let CopyMem run. */
	if (d < s || d >= s + cnt)
	{
		CopyMem(src, dst, cnt);
		return dst;
	}

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
