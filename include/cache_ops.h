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
 * A buffer the DEVICE WRITES needs BOTH ops — a pre-arm op BEFORE it is armed
 * to hardware and invalidate after DMA, on a 64-byte-aligned whole-line range
 * (else bounce).  Never elide either: the pre-arm op guarantees no dirty line
 * covers the buffer during the transfer (eviction would land stale bytes on
 * the payload, and nothing done afterwards can undo that); the post-DMA
 * invalidate drops lines the prefetcher/speculation refilled mid-transfer.
 */

#ifndef _CACHE_OPS_H
#define _CACHE_OPS_H

/* Callers must include <proto/exec.h> (their own __NOLIBBASE__ convention)
 * BEFORE this header — the LVO fallbacks. */
#include <exec/types.h>
#include <exec/execbase.h> /* struct ExecBase, DMA_ReadFromRAM */
#include <barrier.h> /* emu68_barrier() — batch terminator */

/*
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
#ifndef DMAF_NoSync
#define DMAF_NoSync (1L << 4)
#endif

/* Private direction bit (bit 5; mirrors the NDK's DMA_ReadFromRAM naming but
 * is NOT an NDK flag).  The two direction bits give cache_pre_dma a total
 * truth table, mapping 1:1 onto Linux's DMA directions:
 *
 *   DMA_ReadFromRAM   device reads RAM    clean        (cvac)   DMA_TO_DEVICE
 *   DMA_WriteToRAM    device writes RAM   invalidate   (ivac)   DMA_FROM_DEVICE
 *   both              reads and writes    clean+inval  (civac)  DMA_BIDIRECTIONAL
 *   neither           unknown (default)   clean+inval  (civac)  — conservative
 *
 * DMA_WriteToRAM is a PROMISE, not just a direction: the device's writes are
 * all that matters — no CPU-written byte in the range needs to reach RAM, so
 * dirty lines are discarded instead of written back (saves the DRAM writeback
 * of content the device is about to overwrite).  Legal for pure RX/IN payload
 * destinations.  ILLEGAL for any structure whose CPU-initialized state must
 * reach RAM first — event ring segments (zeroed cycle bits), CQ rings (phase
 * bits), contexts, stream arrays, TSB: those are BIDIRECTIONAL (pass 0 or
 * both bits), same trap as Linux FROM_DEVICE vs BIDIRECTIONAL.
 *
 * The LVO tier and 68040.library mask the bit out and fall back to
 * clean+invalidate — a safe superset — so forwarding it is always harmless. */
#ifndef DMA_WriteToRAM
#define DMA_WriteToRAM (1L << 5)
#endif

/* ------------------------------------------------------ inline fast path ---
 *
 * The LINE-F range opcode emitted DIRECTLY, skipping the exec LVO round-trip
 * (~3 JIT dispatcher transitions + block-exit state flushes per call — the
 * dominant cost for small ranges).  Encoding mirrors 68040.library exactly:
 * base always A0, length always D1.L; ext word 0x18/0x1A/0x1C/0x1E00 selects
 * civac / civac+NS / cvac / cvac+NS; op word 0xF440 = invalidate (ivac).
 *
 * Only an Emu68 with the dcache extensions decodes this opcode — anywhere
 * else it Line-F traps.  The firmware advertises support as the /emu68 node's
 * "dcache-range-ops" property; drivers built for the inline path MUST gate
 * device init on emu68_has_dcache_range_ops() (emu68_features.h) and refuse
 * to load when it is absent.
 *
 * Define EMU68_FORCE_LVO_CACHE_OPS to route cache_pre_dma()/cache_post_dma()
 * through the exec LVO instead — set by the EMU68_FORCE_LVO_CACHE_OPS CMake
 * option (see emu68-common/cmake/Emu68CommonCacheOps.cmake).  Both settings
 * ship: ON builds the standard archives that run on any Emu68 release, OFF
 * builds the "-rangeops" archives with the inline fast path.
 * A zero-length range is safe in both paths (cbz-skipped).
 *
 * The invalidate (0xF440) discards whole cache lines and, unlike the exec
 * vectors, makes no concession at the ends of the range: every line it touches
 * is dropped, so a line shared with a neighbour loses whatever that neighbour
 * had dirtied.  Buffers passed to emu68_dcache_invalidate() must therefore own
 * every line they cover — dma_alloc()/dma_zalloc() guarantee it for
 * align >= DMA_ALIGN_MIN, which rounds the size up as well as the base.
 * Anything that cannot promise that has to go through the exec LVO.
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

/* Driver-facing wrappers.  `flags` takes DMA_ReadFromRAM / DMA_WriteToRAM /
 * DMAF_NoSync combinations (direction table above) — pass compile-time
 * constants and the branches fold away. */
static inline void cache_pre_dma(APTR addr, ULONG len, ULONG flags)
{
#ifdef EMU68_FORCE_LVO_CACHE_OPS
	ULONG l = len;
	CachePreDMA(addr, &l, flags);
#else
	ULONG dir = flags & (DMA_ReadFromRAM | DMA_WriteToRAM);
	if (dir == DMA_ReadFromRAM)
		emu68_dcache_clean(addr, len, (flags & DMAF_NoSync) != 0);
	else if (dir == DMA_WriteToRAM)
		emu68_dcache_inv(addr, len, (flags & DMAF_NoSync) != 0);
	else /* neither or both: bidirectional / conservative default */
		emu68_dcache_clean_inv(addr, len, (flags & DMAF_NoSync) != 0);
#endif
}

/* Invalidate-only by design: a range armed with DMA_ReadFromRAM (or one the
 * device did not modify) must simply not be passed here — no site does, so
 * there is no runtime flag check; the exec vector, not this path, serves
 * generic callers.  The only honoured flag bit is DMAF_NoSync. */
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
