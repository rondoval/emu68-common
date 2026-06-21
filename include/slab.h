// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _SLAB_H
#define _SLAB_H

#include <types.h>
#include <dma_mem.h> /* struct dma_pool, dma_alloc/dma_free; pulls in memory.h */

struct slab_node {
	struct slab_node *next;
	void             *data;
};

struct slab_cache {
	void             *free_list;
	APTR              meta_pool; /* Exec pool: slab nodes (+ data when dma_pool == NULL) */
	struct dma_pool  *dma_pool;  /* region pool: DMA data; NULL => CPU-only slab */
	struct slab_node *slabs;
	ULONG             obj_size;
	ULONG             obj_align;
	ULONG             slab_capacity;
};

/* @dma_pool == NULL makes a CPU-only slab (data from @meta_pool); a non-NULL
 * @dma_pool makes the data DMA-reachable (Emu68 RAM). */
void  slab_cache_init(struct slab_cache *cache, APTR meta_pool, struct dma_pool *dma_pool,
                      ULONG obj_size, ULONG obj_align, ULONG slab_capacity);
void  slab_cache_destroy(struct slab_cache *cache);
void *slab_grow(struct slab_cache *cache);

static inline void *slab_alloc(struct slab_cache *cache)
{
	void *ptr = cache->free_list;
	if (likely(ptr)) {
		cache->free_list = *(void **)ptr;
		return ptr;
	}
	return slab_grow(cache);
}

static inline void slab_free(struct slab_cache *cache, void *ptr)
{
	*(void **)ptr = cache->free_list;
	cache->free_list = ptr;
}

static inline void *slab_zalloc(struct slab_cache *cache)
{
	void *ptr = slab_alloc(cache);
	if (ptr)
		memset(ptr, 0, cache->obj_size);
	return ptr;
}

#endif
