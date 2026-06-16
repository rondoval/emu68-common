// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/*
 * dma_mem.c — DMA-safe (Emu68/Pi-DRAM) memory discovery, reachability predicate,
 *             and a region-restricted pool.  See dma_mem.h for the rationale.
 *
 * The Emu68 RAM regions are taken from the device-tree /memory node, parsed the same
 * way devicetree.resource's Add_DT_Memory does (m68k is big-endian, so DT cells are
 * read directly).  The region pool sub-allocates from arenas grabbed via the Exec
 * low-level Allocate() on those Emu68 MemHeaders, so every block is guaranteed to be
 * Pi DRAM the DMA engines can reach — even if Emu68 RAM is under pressure.
 *
 * No file-scope mutable state: all discovered data lives in the caller's
 * struct dma_mem_ctx and in heap-allocated pool/puddle structs, so this object is
 * safe to link into ROM-resident drivers.
 */

#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#include <clib/devicetree_protos.h>
extern struct ExecBase *SysBase;
#define EXEC_BASE_NAME SysBase
#else
#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)
#include <proto/exec.h>
#include <proto/devicetree.h>
#endif

#include <exec/execbase.h>
#include <exec/memory.h>

#include <dma_mem.h>
#include <devtree.h>
#include <bits.h>
#include <debug.h>

#define DMA_MEM_2GB 0x80000000UL
/* Default arena grabbed per puddle from Emu68 RAM; large single requests get their
 * own right-sized puddle. */
#define DMA_POOL_PUDDLE_SIZE (128UL * 1024UL)

struct dma_puddle
{
	struct dma_puddle *next;
	struct MemHeader *src; /* system header the arena was Allocate()'d from */
	APTR arena;
	ULONG arena_size;
	struct MemHeader mh; /* private sub-allocator over [arena, arena+arena_size) */
};

struct dma_pool
{
	struct dma_mem_ctx *ctx;
	struct dma_puddle *puddles;
	ULONG puddle_size;
};

void dma_mem_init(struct dma_mem_ctx *ctx)
{
	if (ctx == NULL)
		return;
	ctx->count = 0;

	APTR DeviceTreeBase = OpenResource((CONST_STRPTR) "devicetree.resource");
	if (DeviceTreeBase == NULL)
	{
		Kprintf("[dma_mem] devicetree.resource unavailable; no DMA regions\n");
		return;
	}

	APTR mem_prop = DT_FindProperty(DT_OpenKey((CONST_STRPTR) "/memory"), (CONST_STRPTR) "reg");
	if (mem_prop == NULL)
	{
		Kprintf("[dma_mem] no /memory reg property; no DMA regions\n");
		return;
	}

	const ULONG *reg = DT_GetPropValue(mem_prop);
	ULONG cells = DT_GetPropLen(mem_prop) / sizeof(ULONG);

	/* DT spec defaults (and the convention used throughout this stack): 2 address
	 * cells, 1 size cell.  The root /memory layout is read with DT_GetNumber so
	 * multi-cell values are assembled (not truncated) before the 2GB filter. */
	APTR root = DT_OpenKey((CONST_STRPTR) "/");
	ULONG addr_cells = DT_GetPropertyValueULONG(root, "#address-cells", 2, FALSE);
	ULONG size_cells = DT_GetPropertyValueULONG(root, "#size-cells", 1, FALSE);

	/* Parse the raw /memory window(s): the Pi-DRAM physical extent.  These are used
	 * only to discriminate which MEMF_FAST headers are Emu68 RAM (Zorro III /
	 * accelerator Fast RAM never falls inside them); the authoritative DMA-reachable
	 * ranges are the kept headers' bounds, recorded below. */
	struct dma_mem_region windows[DMA_MEM_MAX_REGIONS];
	u32 window_count = 0;

	ULONG pos = 0;
	while (pos + addr_cells + size_cells <= cells && window_count < DMA_MEM_MAX_REGIONS)
	{
		u64 base = DT_GetNumber(&reg[pos], addr_cells);
		pos += addr_cells;
		u64 size = DT_GetNumber(&reg[pos], size_cells);
		pos += size_cells;

		if (base >= DMA_MEM_2GB || size == 0)
			continue;
		u64 end = base + size;
		if (end < base || end > DMA_MEM_2GB)
			end = DMA_MEM_2GB;

		windows[window_count].start = (ULONG)base;
		windows[window_count].end = (ULONG)end;
		KprintfH("[dma_mem] Pi-DRAM window %lu: %08lx..%08lx\n",
				 (ULONG)window_count, (ULONG)base, (ULONG)end - 1);
		window_count++;
	}

	/* Collect the MEMF_FAST MemHeaders that fall inside a Pi-DRAM window — these are
	 * the Emu68 RAM headers the region pool allocates arenas from.  Each header's
	 * [mh_Lower, mh_Upper) bounds become a DMA-reachable region, so dma_addr_reachable()
	 * matches exactly what the pool can hand out and excludes chip RAM, ConfigDev
	 * space, removed/split sub-ranges and the 2GB straddle. */
	struct ExecBase *eb = EXEC_BASE_NAME;
	Forbid();
	for (struct MemHeader *mh = (struct MemHeader *)eb->MemList.lh_Head;
		 mh->mh_Node.ln_Succ != NULL && ctx->count < DMA_MEM_MAX_REGIONS;
		 mh = (struct MemHeader *)mh->mh_Node.ln_Succ)
	{
		if ((mh->mh_Attributes & MEMF_FAST) == 0)
			continue;

		ULONG lo = (ULONG)mh->mh_Lower;
		ULONG hi = (ULONG)mh->mh_Upper;
		for (u32 i = 0; i < window_count; i++)
		{
			if (lo >= windows[i].start && hi <= windows[i].end)
			{
				ctx->regions[ctx->count].start = lo;
				ctx->regions[ctx->count].end = hi;
				ctx->regions[ctx->count].header = mh;
				ctx->count++;
				break;
			}
		}
	}
	Permit();

	for (u32 i = 0; i < ctx->count; i++)
		KprintfH("[dma_mem] Emu68 DMA region %lu: %08lx..%08lx\n",
				 (ULONG)i, ctx->regions[i].start, ctx->regions[i].end - 1);
	KprintfH("[dma_mem] %lu Emu68 RAM header(s) usable for DMA\n", (ULONG)ctx->count);
}

/* dma_addr_reachable() is now a static inline in dma_mem.h (per-I/O hot path). */

/* --- Region pool ------------------------------------------------------------- */

static struct dma_puddle *dma_pool_grow(struct dma_pool *pool, ULONG need)
{
	ULONG arena_size = need > pool->puddle_size ? need : pool->puddle_size;
	arena_size = ALIGN_UP(arena_size, MEM_BLOCKSIZE);

	APTR arena = NULL;
	struct MemHeader *src = NULL;
	Forbid();
	for (u32 i = 0; i < pool->ctx->count; i++)
	{
		struct MemHeader *mh = (struct MemHeader *)pool->ctx->regions[i].header;
		arena = Allocate(mh, arena_size);
		if (arena)
		{
			src = mh;
			break;
		}
	}
	Permit();
	if (arena == NULL)
	{
		Kprintf("[dma_mem] out of Emu68 RAM for %lu-byte DMA arena\n", arena_size);
		return NULL;
	}

	struct dma_puddle *pud = AllocMem(sizeof(*pud), MEMF_FAST | MEMF_PUBLIC | MEMF_CLEAR);
	if (pud == NULL)
	{
		Forbid();
		Deallocate(src, arena, arena_size);
		Permit();
		return NULL;
	}

	pud->src = src;
	pud->arena = arena;
	pud->arena_size = arena_size;

	/* Private MemHeader managing this arena via Exec Allocate/Deallocate. */
	pud->mh.mh_Attributes = MEMF_FAST;
	pud->mh.mh_First = (struct MemChunk *)arena;
	pud->mh.mh_Lower = arena;
	pud->mh.mh_Upper = (APTR)((ULONG)arena + arena_size);
	pud->mh.mh_Free = arena_size;
	((struct MemChunk *)arena)->mc_Next = NULL;
	((struct MemChunk *)arena)->mc_Bytes = arena_size;

	pud->next = pool->puddles;
	pool->puddles = pud;
	return pud;
}

APTR dma_pool_region_alloc(struct dma_pool *pool, ULONG size)
{
	ULONG need = ALIGN_UP(size, MEM_BLOCKSIZE);

	for (struct dma_puddle *pud = pool->puddles; pud; pud = pud->next)
	{
		APTR ptr = Allocate(&pud->mh, need);
		if (ptr)
			return ptr;
	}

	struct dma_puddle *pud = dma_pool_grow(pool, need);
	if (pud == NULL)
		return NULL;
	return Allocate(&pud->mh, need);
}

void dma_pool_region_free(struct dma_pool *pool, APTR ptr, ULONG size)
{
	if (ptr == NULL)
		return;

	ULONG need = ALIGN_UP(size, MEM_BLOCKSIZE);
	ULONG a = (ULONG)ptr;

	for (struct dma_puddle *pud = pool->puddles; pud; pud = pud->next)
	{
		if (a >= (ULONG)pud->mh.mh_Lower && a < (ULONG)pud->mh.mh_Upper)
		{
			Deallocate(&pud->mh, ptr, need);
			return;
		}
	}
	Kprintf("[dma_mem] region_free: %08lx not from this pool\n", a);
}

struct dma_pool *dma_pool_create(struct dma_mem_ctx *ctx)
{
	if (ctx == NULL || ctx->count == 0)
		return NULL;

	struct dma_pool *pool = AllocMem(sizeof(*pool), MEMF_FAST | MEMF_PUBLIC | MEMF_CLEAR);
	if (pool == NULL)
		return NULL;

	pool->ctx = ctx;
	pool->puddles = NULL;
	pool->puddle_size = DMA_POOL_PUDDLE_SIZE;
	return pool;
}

void dma_pool_delete(struct dma_pool *pool)
{
	if (pool == NULL)
		return;

	struct dma_puddle *pud = pool->puddles;
	while (pud)
	{
		struct dma_puddle *next = pud->next;
		Forbid();
		Deallocate(pud->src, pud->arena, pud->arena_size);
		Permit();
		FreeMem(pud, sizeof(*pud));
		pud = next;
	}
	FreeMem(pool, sizeof(*pool));
}
