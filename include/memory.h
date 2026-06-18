// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _MEMORY_H
#define _MEMORY_H

#include <types.h>

/* AllocPooled/FreePooled below need exec inlines. */
#ifndef EXEC_BASE_NAME
#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)
#endif
#include <proto/exec.h>

#define MEM_ZERO_CLR1_MAX 63UL
#define MEM_ZERO_CLR4_MAX 511UL

void mem_zero_asm_movem_impl(ULONG *dst, ULONG blocks);

static inline ULONG *mem_zero_align_long(APTR dst, ULONG *len)
{
	ULONG addr = (ULONG)dst;

	if (*len && (addr & 1))
	{
		*(UBYTE *)addr = 0;
		addr += sizeof(UBYTE);
		*len -= sizeof(UBYTE);
	}

	if (*len >= sizeof(UWORD) && (addr & 2))
	{
		*(UWORD *)addr = 0;
		addr += sizeof(UWORD);
		*len -= sizeof(UWORD);
	}

	return (ULONG *)addr;
}

static inline void mem_zero_tail(ULONG *dst, ULONG len)
{
	ULONG addr = (ULONG)dst;

	if (len >= sizeof(UWORD))
	{
		*(UWORD *)addr = 0;
		addr += sizeof(UWORD);
		len -= sizeof(UWORD);
	}

	if (len)
		*(UBYTE *)addr = 0;
}

static inline APTR pool_alloc(APTR poolHeader, ULONG size)
{
	size += sizeof(ULONG);
	APTR ptr = AllocPooled(poolHeader, size);
	if (ptr == NULL)
		return NULL;

	*(ULONG *)ptr = size;
	return (UBYTE *)ptr + sizeof(ULONG);
}

static inline void pool_free(APTR poolHeader, APTR ptr)
{
	if (ptr == NULL)
		return;

	UBYTE *raw = (UBYTE *)ptr - sizeof(ULONG);
	ULONG size = *(ULONG *)raw;
	FreePooled(poolHeader, raw, size);
}

static inline void mem_zero_asm_clr1(APTR dst, ULONG len)
{
	ULONG *d32 = mem_zero_align_long(dst, &len);
	ULONG long_count = len / sizeof(ULONG);
	ULONG tail = len & (sizeof(ULONG) - 1);

	if (long_count)
	{
		asm volatile(
			"move.l %[count], %%d0\n\t"
			"1:\n\t"
			"clr.l (%[dst])+\n\t"
			"subq.l #1, %%d0\n\t"
			"bne.s 1b\n\t"
			: [dst] "+a"(d32)
			: [count] "r"(long_count)
			: "d0", "cc", "memory");
	}

	mem_zero_tail(d32, tail);
}

static inline void mem_zero_asm_clr4(APTR dst, ULONG len)
{
	ULONG *d32 = mem_zero_align_long(dst, &len);
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
			"clr.l (%[dst])+\n\t"
			"clr.l (%[dst])+\n\t"
			"clr.l (%[dst])+\n\t"
			"clr.l (%[dst])+\n\t"
			"subq.l #1, %%d0\n\t"
			"bne.s 1b\n\t"
			"2:\n\t"
			"move.l %[rem], %%d0\n\t"
			"beq.s 4f\n\t"
			"3:\n\t"
			"clr.l (%[dst])+\n\t"
			"subq.l #1, %%d0\n\t"
			"bne.s 3b\n\t"
			"4:\n\t"
			: [dst] "+a"(d32)
			: [quads] "r"(quads), [rem] "r"(rem)
			: "d0", "cc", "memory");
	}

	mem_zero_tail(d32, tail);
}

static inline void mem_zero_asm_movem(APTR dst, ULONG len)
{
	ULONG *d32 = mem_zero_align_long(dst, &len);
	ULONG long_count = len / sizeof(ULONG);
	ULONG blocks = long_count / 16;
	ULONG rem = long_count % 16;
	ULONG tail = len & (sizeof(ULONG) - 1);

	if (blocks)
	{
		mem_zero_asm_movem_impl(d32 + (blocks * 16), blocks);
		d32 += blocks * 16;
	}

	while (rem)
	{
		*d32++ = 0;
		rem--;
	}

	mem_zero_tail(d32, tail);
}

static inline void mem_zero(APTR dst, ULONG len)
{
	if (len <= MEM_ZERO_CLR1_MAX)
	{
		mem_zero_asm_clr1(dst, len);
		return;
	}

	if (len <= MEM_ZERO_CLR4_MAX)
	{
		mem_zero_asm_clr4(dst, len);
		return;
	}

	mem_zero_asm_movem(dst, len);
}

static inline APTR pool_zalloc(APTR poolHeader, ULONG size)
{
	APTR ptr = pool_alloc(poolHeader, size);
	if (ptr)
		mem_zero(ptr, size);
	return ptr;
}

#endif