// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
#ifndef _SLAB_H
#define _SLAB_H

#include <types.h>
#include <memory.h>

struct slab_node {
	struct slab_node *next;
	void             *data;
};

struct slab_cache {
	void             *free_list;
	APTR              pool;
	struct slab_node *slabs;
	ULONG             obj_size;
	ULONG             obj_align;
	ULONG             slab_capacity;
};

void  slab_cache_init(struct slab_cache *cache, APTR pool,
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
		mem_zero(ptr, cache->obj_size);
	return ptr;
}

#endif
