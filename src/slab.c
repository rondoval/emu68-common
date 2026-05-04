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

void slab_cache_init(struct slab_cache *cache, APTR pool,
                     ULONG obj_size, ULONG obj_align, ULONG slab_capacity)
{
	if (obj_align < DMA_ALIGN_MIN)
		obj_align = DMA_ALIGN_MIN;

	obj_size = ALIGN_UP(obj_size, obj_align);

	if (slab_capacity == 0) {
		slab_capacity = SLAB_DEFAULT_SIZE / obj_size;
		if (slab_capacity < 4)
			slab_capacity = 4;
	}

	cache->free_list     = NULL;
	cache->pool          = pool;
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
		dma_free(cache->pool, node->data);
		pool_free(cache->pool, node);
		node = next;
	}

	cache->free_list = NULL;
	cache->slabs     = NULL;
}

void *slab_grow(struct slab_cache *cache)
{
	struct slab_node *node = pool_alloc(cache->pool, sizeof(*node));
	if (!node)
		return NULL;

	ULONG data_size = cache->obj_size * cache->slab_capacity;
	void *data = dma_alloc(cache->pool, cache->obj_align, data_size);
	if (!data) {
		pool_free(cache->pool, node);
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

