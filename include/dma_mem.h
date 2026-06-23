// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _DMA_MEM_H
#define _DMA_MEM_H

#include <types.h>
#include <memory.h> /* memset (dma_zalloc), pool_* live here */

/*
 * DMA-safe memory for PiStorm/Emu68.
 *
 * Only Emu68 Fast RAM (the Pi's own DRAM, advertised in the device-tree /memory
 * node and added to Exec by devicetree.resource's Add_DT_Memory as "expansion
 * memory") is reachable by the Pi's PCIe / on-SoC DMA engines.  Chip RAM and any
 * Zorro III / accelerator Fast RAM are NOT reachable.  Emu68 RAM is added at a high
 * priority so AllocMem(MEMF_FAST) usually returns it, but that is not guaranteed
 * once Emu68 RAM is exhausted.
 *
 * This facility discovers the Emu68 RAM regions once (from /memory) and provides:
 *   - dma_addr_reachable(): a transport-agnostic predicate (works for PCIe and the
 *     on-SoC genet alike) for bounce-buffer decisions;
 *   - dma_pool_create(): a region-restricted pool that *always* allocates from Emu68
 *     RAM, so a driver's persistent DMA structures and bounce buffers are guaranteed
 *     reachable even under Emu68-RAM pressure.  A struct dma_pool * handle is ONLY
 *     valid for the dma_alloc/dma_zalloc/dma_free family below; CPU-only metadata uses
 *     an ordinary Exec pool (CreatePool) via memory.h's pool_alloc/pool_free.
 *
 * The discovered state lives in a caller-owned struct dma_mem_ctx (embed it in the
 * device base / controller struct).
 */

#define DMA_MEM_MAX_REGIONS 8

struct dma_mem_region
{
	ULONG start;  /* inclusive */
	ULONG end;	  /* exclusive */
	APTR  header; /* struct MemHeader * the pool Allocate()s arenas from */
};

struct dma_mem_ctx
{
	u32 count; /* entries in regions[]; each carries its own bounds + header */
	struct dma_mem_region regions[DMA_MEM_MAX_REGIONS];
};

/* Discover the Emu68 RAM regions into @ctx (clears and fills it).  Call once early in
 * driver init, before dma_pool_create()/dma_addr_reachable(). */
void dma_mem_init(struct dma_mem_ctx *ctx);

/* TRUE iff [addr, addr+len) lies entirely within Emu68 (DMA-reachable) RAM.
 * Returns FALSE if @ctx is NULL or found no regions (fail safe -> caller bounces).
 */
static inline BOOL dma_addr_reachable(struct dma_mem_ctx *ctx, APTR addr, ULONG len)
{
	if (ctx == NULL)
		return FALSE;

	ULONG a = (ULONG)addr;
	ULONG end = a + (len ? len : 1);

	if (end < a) /* wrap */
		return FALSE;

	/* No device-tree region info: degrade to the historical "above Chip RAM"
	 * heuristic so behaviour is no worse than before this facility existed. */
	if (ctx->count == 0)
		return a >= 0x00200000UL;

	for (u32 i = 0; i < ctx->count; i++)
	{
		if (a >= ctx->regions[i].start && end <= ctx->regions[i].end)
			return TRUE;
	}
	return FALSE;
}

/* Opaque region-pool handle.  Created only by dma_pool_create() */
struct dma_pool;

/* Create/destroy a region-restricted pool over @ctx's Emu68 RAM (allocations come
 * from Emu68 RAM).  @ctx must outlive the pool.  Returns NULL if no region. */
struct dma_pool *dma_pool_create(struct dma_mem_ctx *ctx);
void dma_pool_delete(struct dma_pool *pool);

/* --- Region sub-allocator (raw); prefer the dma_alloc/dma_zalloc/dma_free helpers
 *     below, which add the cache-line alignment + size bookkeeping. --- */
APTR dma_pool_region_alloc(struct dma_pool *pool, ULONG size);
void dma_pool_region_free(struct dma_pool *pool, APTR ptr, ULONG size);

/*
 * dma_alloc/dma_zalloc/dma_free — DMA-buffer allocation from a region pool.
 *
 * A struct dma_pool * is required: the allocation is guaranteed to land in Emu68
 * (DMA-reachable) RAM.  dma_alloc rounds cache-line (or coarser) aligned requests up
 * so the buffer owns whole cache lines at BOTH ends (a partial trailing line shared
 * with the next allocation would be discarded by the post-DMA invalidate — see the
 * 68040.library CachePreDMA/PostDMA contract).
 */
static inline void *dma_alloc(struct dma_pool *pool, ULONG align, ULONG size)
{
	if (align < sizeof(APTR))
		align = sizeof(APTR);

	if (align >= DMA_ALIGN_MIN)
		size = (size + (align - 1)) & ~(align - 1);

	ULONG total = size + (align - 1) + sizeof(APTR) + sizeof(ULONG);
	APTR raw = dma_pool_region_alloc(pool, total);
	if (!raw)
		return NULL;

	APTR aligned = (APTR)(((ULONG)raw + sizeof(ULONG) + sizeof(APTR) + align - 1) & ~(align - 1));
	((APTR *)aligned)[-1] = raw;
	*(ULONG *)raw = total;

	return aligned;
}

static inline void dma_free(struct dma_pool *pool, void *ptr)
{
	if (ptr)
	{
		APTR raw = ((APTR *)ptr)[-1];
		ULONG size = *(ULONG *)raw;
		dma_pool_region_free(pool, raw, size);
	}
}

static inline void *dma_zalloc(struct dma_pool *pool, ULONG align, ULONG size)
{
	void *ptr = dma_alloc(pool, align, size);
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

#endif /* _DMA_MEM_H */
