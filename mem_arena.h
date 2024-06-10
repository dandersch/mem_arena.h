#pragma once

struct mem_arena_t;
typedef struct mem_arena_t mem_arena_t;

#include <stddef.h>  // for size_t
#include <stdint.h>  // for uintptr_t
#include <string.h>  // for memset

/* NOTE: used when arena is supposed to just alloc and free (not reserve & commit) */
#ifndef MEM_ARENA_OS_ALLOC // TODO unused right now
  #include <stdlib.h>
  #define MEM_ARENA_OS_ALLOC(size) malloc(size)
  #define MEM_ARENA_OS_FREE(ptr)   free(ptr)
#endif

#ifndef MEM_ARENA_ASSERT
    #include <assert.h>
    #define MEM_ARENA_ASSERT(expr) assert(expr)
#endif

/* NOTE: if one macro related to commiting/reserving memory is defined, we
   assume the arena is supposed to reserve and commit and not use malloc() */
#if defined(MEM_ARENA_OS_COMMIT) || defined(MEM_ARENA_OS_RESERVE) || defined(MEM_ARENA_OS_DECOMMIT) || defined(MEM_ARENA_OS_RELEASE)
  #ifndef MEM_ARENA_OS_COMMIT
    #error "No memory commit function defined"
  #endif
  #ifndef MEM_ARENA_OS_DECOMMIT
    #error "No memory decommit function defined"
  #endif
  #ifndef MEM_ARENA_OS_RESERVE
    #error "No memory reserve function defined"
  #endif
  #ifndef MEM_ARENA_OS_RELEASE
    #error "No memory release function defined"
  #endif
#endif

/* api */
mem_arena_t* mem_arena_reserve (size_t size_in_bytes);            /* create an arena w/ reserved memory TODO rename */
void*        mem_arena_push    (mem_arena_t* arena, size_t size); /* push on the arena, committing if needed  */
void*        mem_arena_place   (mem_arena_t* arena, size_t size); /* push on the arena w/o committing memory  */
mem_arena_t* mem_arena_subarena(mem_arena_t* base,  size_t size); /* pushes on an arena w/o committing memory */
void         mem_arena_pop_to  (mem_arena_t* arena, void* buf);   /* TODO doesn't decommit for now */
void         mem_arena_pop_by  (mem_arena_t* arena, size_t bytes);
void         mem_arena_free    (mem_arena_t* arena);

/* helper */
mem_arena_t* mem_arena_default ();
size_t       mem_arena_get_pos (mem_arena_t* arena);
#define ARENA_PUSH_ARRAY(arena, type, count) (type*) mem_arena_push((arena), sizeof(type)*(count))
#define ARENA_PUSH_STRUCT(arena, type)       ARENA_PUSH_ARRAY((arena), type, 1)
#define ARENA_BUFFER(arena, pos)             ((void*) ((((unsigned char*) arena) + sizeof(mem_arena_t)) + pos))

#define ARENA_DEFAULT_RESERVE_SIZE (4 * 1024 * 1024)

/* TODO scratch arenas */

#ifdef MEM_ARENA_IMPLEMENTATION
struct mem_arena_t
{
    size_t pos;
    size_t cap;
    size_t commit_pos;

    #ifdef BUILD_DEBUG
    int depth;         // base arena has depth 0
    size_t commit_amount;
    #endif
};

mem_arena_t* mem_arena_reserve(size_t size_in_bytes) {
    mem_arena_t* arena = (mem_arena_t*) MEM_ARENA_OS_RESERVE(size_in_bytes + sizeof(mem_arena_t));

    /* commit enough to write the arena metadata */
    MEM_ARENA_OS_COMMIT((void*) arena, sizeof(mem_arena_t));

    // TODO assert

    arena->pos        = 0;
    arena->cap        = size_in_bytes;
    arena->commit_pos = 0;

    #ifdef BUILD_DEBUG
    arena->depth         = 0;
    arena->commit_amount = 0;
    #endif

    return arena;
}
mem_arena_t* mem_arena_subarena(mem_arena_t* base, size_t size) {
    /* push on an arena w/o committing memory */
    mem_arena_t* subarena = NULL;
    if ((base->pos + size) <= base->cap)
    {
        subarena         = (mem_arena_t*) ARENA_BUFFER(base, base->pos);
        base->pos        = base->pos + size;
        base->commit_pos = base->pos + size; // NOTE we advance the commit_pos
                                             // here even though we don't commit
                                             // the memory - maybe we should
                                             // rename this member
    }
    else { MEM_ARENA_ASSERT(0 && "Couldn't fit subarena\n"); }

    /* commit enough to write the subarena metadata */
    MEM_ARENA_OS_COMMIT((void*) subarena, sizeof(mem_arena_t)); // TODO handle error
    subarena->pos         = 0;
    subarena->cap         = size;
    subarena->commit_pos  = 0; // TODO should we set it to 0 or to sizeof(mem_arena_t)

    #ifdef BUILD_DEBUG
    subarena->depth         = base->depth + 1;
    subarena->commit_amount = 0;
    #endif

    return subarena;
}
void* mem_arena_push(mem_arena_t* arena, size_t size) {
    void* buf   = NULL;
    size_t push_to = arena->pos + size;
    if (push_to <= arena->cap)
    {
        buf = ARENA_BUFFER(arena, arena->pos);
        arena->pos = push_to;

        /* handle committing */
        if (arena->commit_pos <= arena->pos)
        {
            int committed = MEM_ARENA_OS_COMMIT(ARENA_BUFFER(arena, arena->commit_pos), size);
            MEM_ARENA_ASSERT(committed);
            arena->commit_pos += size;

            #ifdef BUILD_DEBUG
            arena->commit_amount += size;
            #endif
        }
    }
    else { MEM_ARENA_ASSERT(0 && "Overstepped capacity of arena"); }
    MEM_ARENA_ASSERT(buf);
    return buf;
}
void mem_arena_pop_to(mem_arena_t* arena, void* buf) {
    MEM_ARENA_ASSERT((uintptr_t) arena <= (uintptr_t) buf);
    MEM_ARENA_ASSERT(((uintptr_t) arena + arena->cap) >= (uintptr_t) buf);
    size_t new_pos =  (unsigned char*) buf - (unsigned char*) ARENA_BUFFER(arena, 0);
    size_t diff    = arena->pos - new_pos;
    if (diff > 0)
    {
        arena->pos = new_pos;
        memset(ARENA_BUFFER(arena, arena->pos), 0, diff);

        /* TODO handle decommitting here */
    }
}
void mem_arena_pop_by(mem_arena_t* arena, size_t bytes) {
    MEM_ARENA_ASSERT((arena->pos - bytes) >= 0);
    mem_arena_pop_to(arena, ARENA_BUFFER(arena, arena->pos - bytes));
}
void mem_arena_free(mem_arena_t* arena) {
    size_t cap = arena->cap;
    MEM_ARENA_OS_DECOMMIT((void*) arena, cap);
    MEM_ARENA_OS_RELEASE((void*) arena, cap);
}

mem_arena_t* mem_arena_default() {
    mem_arena_t* default_arena = mem_arena_reserve(ARENA_DEFAULT_RESERVE_SIZE);
    return default_arena;
}
size_t mem_arena_get_pos(mem_arena_t* arena) {
    return arena->pos;
}
#endif // MEM_ARENA_IMPLEMENTATION
