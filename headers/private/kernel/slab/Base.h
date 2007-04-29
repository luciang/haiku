/*
 * Copyright 2007, Hugo Santos. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *      Hugo Santos, hugosantos@gmail.com
 */
#ifndef _SLAB_BASE_SLAB_H_
#define _SLAB_BASE_SLAB_H_

#include <KernelExport.h>
#include <OS.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	/* create_object_cache_etc flags */
	CACHE_NO_DEPOT			= 1 << 0,

	/* object_cache_alloc flags */
	CACHE_DONT_SLEEP		= 1 << 8,

	/* internal */
	CACHE_DURING_BOOT		= 1 << 31
};

typedef struct object_cache object_cache;

typedef status_t (*object_cache_constructor)(void *cookie, void *object);
typedef void (*object_cache_destructor)(void *cookie, void *object);
typedef void (*object_cache_reclaimer)(void *cookie, int32 level);

object_cache *create_object_cache(const char *name, size_t object_size,
	size_t alignment, void *cookie, object_cache_constructor constructor,
	object_cache_destructor);
object_cache *create_object_cache_etc(const char *name, size_t object_size,
	size_t alignment, size_t max_byte_usage, uint32 flags, void *cookie,
	object_cache_constructor constructor, object_cache_destructor destructor,
	object_cache_reclaimer reclaimer);

void delete_object_cache(object_cache *cache);

void *object_cache_alloc(object_cache *cache, uint32 flags);
void object_cache_free(object_cache *cache, void *object);

#ifdef __cplusplus
}
#endif

#endif
