// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _MEMORY_H
#define _MEMORY_H

#include <types.h>

/* pool_alloc/pool_free below expand exec inlines at the call site, so they
 * use the caller's EXEC_BASE_NAME: a local SysBase (never $4). */
#include <proto/exec.h>

/* Freestanding C runtime memory primitives (memory.c, memset_movem.S,
 * memcpy_movem.S).  These are the symbols GCC may synthesise at -O3 in this
 * -nostdlib tree.  memset and memcpy are asm: a longword loop for short sizes,
 * movem.l blocks for long ones (any alignment, 68020+; no Exec call, so they
 * are callable from interrupts and from SANA-II copy hooks, and ROM-safe);
 * memmove forwards to memcpy or copies descending, with the same guarantee.
 * Signatures match the compiler builtins so call sites in builtin-enabled TUs
 * may still be optimised (e.g. a constant-size memset(&x, 0, sizeof x) inlined
 * directly).
 *
 * The length type is the compiler's __SIZE_TYPE__ (== the builtin memset/memcpy
 * size_t) used directly rather than via <stddef.h>: this header is pulled into
 * TUs that redefine size_t themselves (e.g. emu68-pcie-library's u64 size_t), so
 * naming the underlying type keeps the builtin ABI and avoids a typedef clash. */
void *memset(void *dst, int c, __SIZE_TYPE__ n);
void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n);
void *memmove(void *dst, const void *src, __SIZE_TYPE__ n);
int memcmp(const void *s1, const void *s2, __SIZE_TYPE__ n);

/* Pooled allocations with a size header, so pool_free needs no size.
 * Macros, not inline functions: the AllocPooled/FreePooled call must expand
 * in the caller, where EXEC_BASE_NAME may be a local SysBase cached in fast
 * RAM (reading $4 is an Amiga-bus cycle on PiStorm) - an inline function body
 * has no such local. */
#define pool_alloc(poolHeader, size) ({                                      \
	ULONG pool_size_ = (ULONG)(size) + sizeof(ULONG);                        \
	ULONG *pool_raw_ = (ULONG *)AllocPooled((poolHeader), pool_size_);       \
	if (pool_raw_ != NULL)                                                   \
		*pool_raw_++ = pool_size_;                                           \
	(APTR)pool_raw_;                                                         \
})

#define pool_free(poolHeader, ptr) do {                                      \
	APTR pool_ptr_ = (ptr);                                                  \
	if (pool_ptr_ != NULL)                                                   \
	{                                                                        \
		ULONG *pool_raw_ = (ULONG *)pool_ptr_ - 1;                           \
		FreePooled((poolHeader), pool_raw_, *pool_raw_);                     \
	}                                                                        \
} while (0)

#define pool_zalloc(poolHeader, size) ({                                     \
	ULONG pool_zsize_ = (ULONG)(size);                                       \
	APTR pool_zptr_ = pool_alloc((poolHeader), pool_zsize_);                 \
	if (pool_zptr_ != NULL)                                                  \
		memset(pool_zptr_, 0, pool_zsize_);                                  \
	pool_zptr_;                                                              \
})

#endif /* _MEMORY_H */
