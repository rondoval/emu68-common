// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#else
#define __NOLIBBASE__
#define EXEC_BASE_NAME (*(struct ExecBase **)4UL)
#include <proto/exec.h>
#endif

#include <slab.h>
#include <memory.h>
#include <bits.h>

#define SLAB_DEFAULT_SIZE 262144UL

void slab_cache_init(struct slab_cache *cache, APTR meta_pool, struct dma_pool *dma_pool,
                     ULONG obj_size, ULONG obj_align, ULONG slab_capacity)
{
	/* DMA data must own whole cache lines; CPU-only data just needs to thread the
	 * free list, so pointer alignment is enough. */
	ULONG min_align = dma_pool ? DMA_ALIGN_MIN : sizeof(APTR);
	if (obj_align < min_align)
		obj_align = min_align;

	obj_size = ALIGN_UP(obj_size, obj_align);

	if (slab_capacity == 0) {
		slab_capacity = SLAB_DEFAULT_SIZE / obj_size;
		if (slab_capacity < 4)
			slab_capacity = 4;
	}

	cache->free_list     = NULL;
	cache->meta_pool     = meta_pool;
	cache->dma_pool      = dma_pool;
	cache->slabs         = NULL;
	cache->obj_size      = obj_size;
	cache->obj_align     = obj_align;
	cache->slab_capacity = slab_capacity;
}

void slab_cache_destroy(struct slab_cache *cache)
{
	struct slab_node *node = cache->slabs;

	while (node) {
		struct slab_node *next = node->next;
		if (cache->dma_pool)
			dma_free(cache->dma_pool, node->data);
		else
			pool_free(cache->meta_pool, node->data);
		pool_free(cache->meta_pool, node);
		node = next;
	}

	cache->free_list = NULL;
	cache->slabs     = NULL;
}

void *slab_grow(struct slab_cache *cache)
{
	struct slab_node *node = pool_alloc(cache->meta_pool, sizeof(*node));
	if (!node)
		return NULL;

	ULONG data_size = cache->obj_size * cache->slab_capacity;
	void *data = cache->dma_pool
		? dma_alloc(cache->dma_pool, cache->obj_align, data_size)
		: pool_alloc(cache->meta_pool, data_size);
	if (!data) {
		pool_free(cache->meta_pool, node);
		return NULL;
	}

	node->data  = data;
	node->next  = cache->slabs;
	cache->slabs = node;

	ULONG sz = cache->obj_size;
	const ULONG n  = cache->slab_capacity;
	char *slot = (char *)data;

	for (ULONG i = 0; i < n - 1; i++, slot += sz)
		*(void **)slot = slot + sz;

	*(void **)slot  = cache->free_list;
	/* slot[0] is returned to the caller; chain head starts at slot[1] (or
	 * the previous free_list if n == 1). */
	cache->free_list = *(void **)data;

	return data;
}

