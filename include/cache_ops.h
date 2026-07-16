// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * cache_ops.h — the Emu68 DMA cache-maintenance and the inline fast path.
 *
 * Exec's CachePreDMA/CachePostDMA are patched by 68040.library (embedded in
 * the Emu68 image — the pair is atomic), which emits a private LINE-F range
 * opcode that Emu68 JIT-compiles to a per-64-byte-line dc loop closed by one
 * dsb sy.  Semantics:
 *
 *   CachePreDMA(a, &l, 0)               clean+invalidate (dc civac)
 *   CachePreDMA(a, &l, DMA_ReadFromRAM) clean only       (dc cvac)
 *   CachePostDMA(a, &l, 0)              invalidate       (dc ivac)
 *
 * A buffer the DEVICE WRITES needs BOTH ops — clean+invalidate BEFORE it is
 * armed to hardware and invalidate after DMA, on a 64-byte-aligned whole-line
 * range (else bounce).  Never elide the pre-arm op: a dirty line at DMA time
 * corrupts the payload by eviction or by the post-invalidate itself (Cortex-A
 * executes dc ivac on a dirty line as clean+invalidate).
 *
 * DMAF_NoSync (private flag, bit 4; NDK uses bits 1-3) suppresses the range
 * op's trailing dsb sy so a BATCH of ops pays one barrier:
 *
 *   Batch patterns (prefer the first when the loop bound is known):
 *     1. every op but the LAST carries DMAF_NoSync;
 *     2. every op carries DMAF_NoSync, then one emu68_barrier().
 *
 *   A zero-length range op emits NOTHING (the handler cbz-skips it) — it is
 *   never a barrier.  On a pre-NoSync Emu68 the bit is ignored at translation
 *   time (per-op barrier still emitted), so setting it is always safe.
 *
 *   SHARP EDGE: NoSync on an INVALIDATE is only legal if a dsb (a non-NoSync
 *   op or emu68_barrier()) executes before ANY CPU read of the region — a
 *   load is not ordered after dc ivac without one.
 */
#ifndef _CACHE_OPS_H
#define _CACHE_OPS_H

/* Callers must include <proto/exec.h> (their own __NOLIBBASE__ convention)
 * BEFORE this header — the LVO fallbacks. */
#include <exec/types.h>
#include <exec/execbase.h> /* struct ExecBase, DMA_ReadFromRAM */
#include <barrier.h> /* emu68_barrier() — batch terminator */

#ifndef DMAF_NoSync
#define DMAF_NoSync (1L << 4)
#endif

/* ------------------------------------------------------ inline fast path ---
 *
 * The LINE-F range opcode emitted DIRECTLY, skipping the exec LVO round-trip
 * (~3 JIT dispatcher transitions + block-exit state flushes per call — the
 * dominant cost for small ranges).  Encoding mirrors 68040.library exactly:
 * base always A0, length always D1.L; ext word 0x18/0x1A/0x1C/0x1E00 selects
 * civac / civac+NS / cvac / cvac+NS; op word 0xF440 = invalidate (ivac).
 *
 * Emitted UNCONDITIONALLY: these drivers exist only on PiStorm/Emu68 with
 * the embedded 68040.library — the same assumption timing.h's raw MMIO reads
 * already make.  (On a bare Emu68 without the range-op handler this opcode
 * would Line-F trap; that configuration is unsupported, exactly like the
 * missing Pi system timer would be.)
 *
 * Define EMU68_FORCE_LVO_CACHE_OPS to route cache_pre_dma()/cache_post_dma()
 * through the exec LVO instead — set by the EMU68_FORCE_LVO_CACHE_OPS CMake
 * option (see emu68-common/cmake/Emu68CommonCacheOps.cmake), which CI turns ON
 * because it builds against a released Emu68 that lacks the range opcode; local
 * builds against a patched Emu68 leave it OFF for the inline fast path.
 * A zero-length range is safe in both paths (cbz-skipped).
 */

static inline void emu68_dcache_clean(const void *addr, ULONG len, BOOL nosync)
{
	register ULONG a0 asm("a0") = (ULONG)addr;
	register ULONG d1 asm("d1") = len;
	if (nosync)
		asm volatile(".short 0xF460\n\t.short 0x1E00" : : "a"(a0), "d"(d1) : "memory");
	else
		asm volatile(".short 0xF460\n\t.short 0x1C00" : : "a"(a0), "d"(d1) : "memory");
}

static inline void emu68_dcache_clean_inv(const void *addr, ULONG len, BOOL nosync)
{
	register ULONG a0 asm("a0") = (ULONG)addr;
	register ULONG d1 asm("d1") = len;
	if (nosync)
		asm volatile(".short 0xF460\n\t.short 0x1A00" : : "a"(a0), "d"(d1) : "memory");
	else
		asm volatile(".short 0xF460\n\t.short 0x1800" : : "a"(a0), "d"(d1) : "memory");
}

static inline void emu68_dcache_inv(const void *addr, ULONG len, BOOL nosync)
{
	register ULONG a0 asm("a0") = (ULONG)addr;
	register ULONG d1 asm("d1") = len;
	if (nosync)
		asm volatile(".short 0xF440\n\t.short 0x1A00" : : "a"(a0), "d"(d1) : "memory");
	else
		asm volatile(".short 0xF440\n\t.short 0x1800" : : "a"(a0), "d"(d1) : "memory");
}

/* Driver-facing wrappers.  `flags` takes the same DMA_ReadFromRAM /
 * DMAF_NoSync combinations as CachePreDMA — pass compile-time constants and
 * the branches fold away. */
static inline void cache_pre_dma(APTR addr, ULONG len, ULONG flags)
{
#ifdef EMU68_FORCE_LVO_CACHE_OPS
	ULONG l = len;
	CachePreDMA(addr, &l, flags);
#else
	if (flags & DMA_ReadFromRAM)
		emu68_dcache_clean(addr, len, (flags & DMAF_NoSync) != 0);
	else
		emu68_dcache_clean_inv(addr, len, (flags & DMAF_NoSync) != 0);
#endif
}

static inline void cache_post_dma(APTR addr, ULONG len, ULONG flags)
{
#ifdef EMU68_FORCE_LVO_CACHE_OPS
	ULONG l = len;
	CachePostDMA(addr, &l, flags);
#else
	emu68_dcache_inv(addr, len, (flags & DMAF_NoSync) != 0);
#endif
}

#endif /* _CACHE_OPS_H */
