#pragma once

#if defined(LANGUAGE_CPP)
  #define TYPE_OF(e) decltype(e)
#elif !defined(COMPILER_MSVC)
  #define TYPE_OF(e) __typeof__(e) /* NOTE: non standard gcc extension, but works everywhere except MSVC w/ C */
#else
  /* no known typeof() for msvc in c-mode */
  #error "no typeof() available"
#endif

#include "mem_arena.h"

// https://www.gingerbill.org/article/2019/02/16/memory-allocation-strategies-004/

typedef struct mem_pool_header_t mem_pool_header_t;
struct mem_pool_header_t { mem_pool_header_t* next; };
typedef struct mem_pool_t
{
    mem_arena_t* arena;
    size_t chunk_size;
    mem_pool_header_t* head;
} mem_pool_t;

#include <stdio.h>
#include <assert.h>
static inline mem_pool_t* mem_pool_create_ex(mem_arena_t* backing_arena, size_t chunk_size, size_t count)
{
    printf("Hello\n");

    size_t capacity_in_bytes = (chunk_size + sizeof(mem_pool_header_t)) * count;
    size_t allocation_size   = capacity_in_bytes + sizeof(mem_pool_t);

    /* init */
    mem_pool_t* pool = (mem_pool_t*) mem_arena_push(backing_arena, allocation_size);
    pool->arena      = backing_arena;
    pool->chunk_size = chunk_size;
    pool->head       = (mem_pool_header_t*) pool + sizeof(mem_pool_t);

    /* init the free list */
    mem_pool_header_t** header = &pool->head;
    for (size_t i = 0; i < count; i++)
    {
        (*header)->next = (mem_pool_header_t*) ((char*) header + chunk_size);
        header          = ((mem_pool_header_t**) &(*header)->next);
    }

    return pool;
}

#define mem_pool_create(arena, type, count) \
    mem_pool_create_ex(arena, sizeof(type), count)

static inline void* mem_pool_alloc_ex(mem_pool_t* pool, size_t chunk_size)
{
    assert(sizeof(chunk_size) == pool->chunk_size); // quasi type check for safety

    if (!pool->head) { printf("Out of space\n"); return NULL; }

    void* free_chunk = (char*) pool->head + sizeof(mem_pool_header_t);

    pool->head = (mem_pool_header_t*) pool->head->next;

    // TODO zero memory by default

    return free_chunk;
}

#define mem_pool_alloc(pool, type) \
    (type*) mem_pool_alloc_ex(pool, sizeof(type))

int mem_pool_free_ex(mem_pool_t* pool, void* chunk, size_t chunk_size); // push onto free list or pop arena

#define mem_pool_free(pool, ptr) \
    mem_pool_free_ex(pool, ptr, sizeof(*ptr)); /* TODO is dereferencing the ptr to get the size here a bad idea? */

/* usage:
 * mem_pool_t* thing_pool = mem_pool_create(thing_t, 1024);
 *
 * thing_t* thing = mem_pool_alloc(thing_pool, thing_t);;
 *
 */
