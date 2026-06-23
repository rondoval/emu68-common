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

/* Freestanding C runtime memory primitives (implemented in memory.c).  These
 * are the symbols GCC may synthesise at -O3 in this -nostdlib tree; memset is an
 * asm-optimised byte fill, memcpy/memmove route through Exec CopyMem.  Signatures
 * match the compiler builtins so call sites in builtin-enabled TUs may still be
 * optimised (e.g. a constant-size memset(&x, 0, sizeof x) inlined directly).
 *
 * The length type is the compiler's __SIZE_TYPE__ (== the builtin memset/memcpy
 * size_t) used directly rather than via <stddef.h>: this header is pulled into
 * TUs that redefine size_t themselves (e.g. emu68-pcie-library's u64 size_t), so
 * naming the underlying type keeps the builtin ABI and avoids a typedef clash. */
void *memset(void *dst, int c, __SIZE_TYPE__ n);
void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n);
void *memmove(void *dst, const void *src, __SIZE_TYPE__ n);
int memcmp(const void *s1, const void *s2, __SIZE_TYPE__ n);

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

static inline APTR pool_zalloc(APTR poolHeader, ULONG size)
{
	APTR ptr = pool_alloc(poolHeader, size);
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

#endif /* _MEMORY_H */
