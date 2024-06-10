#pragma once

/*
 * NOTE:
 * - either define malloc/free-like MEM_ARENA_OS_ALLOC and MEM_ARENA_OS_FREE...
 * - or reserve/commit and release/decommit macros
 *   MEM_ARENA_OS_{RESERVE,COMMIT,DECOMMIT,RELEASE}.
 *
 * The arena will use whichever one was defined, but will prefer a
 * reserve/commit strategy when both are defined. If none are defined, the arena
 * will use malloc and free by default.
 */

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

#include <stddef.h>  // for size_t
#include <stdint.h>  // for uintptr_t
#include <string.h>  // for memset

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

  #define MEM_ARENA_USE_RESERVE_AND_COMMIT_STRATEGY
#endif

struct mem_arena_t;
typedef struct mem_arena_t mem_arena_t;

/* api */
mem_arena_t* mem_arena_create  (size_t        size_in_bytes);
void*        mem_arena_push    (mem_arena_t*  arena, size_t size); /* push onto arena, committing if needed  */

void*        mem_arena_place   (mem_arena_t*  arena, size_t size); /* push onto arena w/o committing memory  */
mem_arena_t* mem_arena_subarena(mem_arena_t*  base,  size_t size); /* pushes on an arena w/o committing memory */

void         mem_arena_pop_to  (mem_arena_t*  arena, char* buf);
void         mem_arena_pop_by  (mem_arena_t*  arena, size_t bytes);

void         mem_arena_clear   (mem_arena_t*  arena);
void         mem_arena_destroy (mem_arena_t** arena);

/* helper */
mem_arena_t* mem_arena_default ();
#define ARENA_PUSH_ARRAY(arena, type, count) (type*) mem_arena_push((arena), sizeof(type)*(count))
#define ARENA_PUSH_STRUCT(arena, type)       ARENA_PUSH_ARRAY((arena), type, 1)

//#define ARENA_BUFFER(arena, pos)             ((void*) ((((char*) arena) + sizeof(mem_arena_t)) + pos))

/* TODO scratch arenas */

#ifdef MEM_ARENA_IMPLEMENTATION
typedef struct arena_region_header_t { size_t size; /* size of allocated region*/ } arena_region_header_t; /* unused */

struct mem_arena_t
{
    char* pos;
    char* end;
    char* commit_pos;

    /* size_t pos; */
    /* size_t cap; */
    /* size_t commit_pos; */

    #ifdef BUILD_DEBUG
    int depth; /* base arena has depth 0 */
    size_t commit_amount; /* actual amount committed when considering subarenas */
    #endif
};

mem_arena_t* mem_arena_create(size_t size_in_bytes) {

    #ifdef MEM_ARENA_USE_RESERVE_AND_COMMIT_STRATEGY
      mem_arena_t* arena = (mem_arena_t*) MEM_ARENA_OS_RESERVE(size_in_bytes + sizeof(mem_arena_t));

      /* commit enough to write the arena metadata */
      MEM_ARENA_OS_COMMIT((void*) arena, sizeof(mem_arena_t));
    #else
      mem_arena_t* arena = (mem_arena_t*) MEM_ARENA_OS_ALLOC(size_in_bytes + sizeof(mem_arena_t));
    #endif

    MEM_ARENA_ASSERT(arena);

    /* arena->pos        = 0; */
    /* arena->cap        = size_in_bytes; */
    /* arena->commit_pos = 0; */
    arena->pos        = (char*) arena + sizeof(mem_arena_t);
    arena->end        = arena->pos + size_in_bytes;
    arena->commit_pos = arena->pos;

    #ifdef BUILD_DEBUG
    arena->depth         = 0;
    arena->commit_amount = 0;
    #endif

    return arena;
}
mem_arena_t* mem_arena_subarena(mem_arena_t* base, size_t size) {
    /* push on an arena w/o committing memory (when MEM_ARENA_USE_RESERVE_AND_COMMIT_STRATEGY) */
    mem_arena_t* subarena = NULL;
    if ((base->pos + (size + sizeof(mem_arena_t)) <= base->end))
    {
        //subarena       = (mem_arena_t*) ARENA_BUFFER(base, base->pos);
        subarena         = (mem_arena_t*) base->pos;
        base->pos        = base->pos + (size + sizeof(mem_arena_t));
        /* NOTE we advance the commit_pos here even though we don't commit the
         * memory - maybe we should rename this member */
        base->commit_pos = base->pos + (size + sizeof(mem_arena_t));
    }
    else { MEM_ARENA_ASSERT(0 && "Couldn't fit subarena\n"); }

    #ifdef MEM_ARENA_USE_RESERVE_AND_COMMIT_STRATEGY
      /* commit enough to write the subarena metadata */
      MEM_ARENA_OS_COMMIT((void*) subarena, sizeof(mem_arena_t)); // TODO handle error
    #endif

    subarena->pos         = (char*) subarena + sizeof(mem_arena_t);
    subarena->end         = subarena->pos + size;
    subarena->commit_pos  = subarena->pos;

    #ifdef BUILD_DEBUG
    subarena->depth         = base->depth + 1;
    subarena->commit_amount = 0;
    #endif

    return subarena;
}
void* mem_arena_push(mem_arena_t* arena, size_t size) {
    void* buf     = NULL;
    char* push_to = arena->pos + size;
    if (push_to <= arena->end)
    {
        //buf = ARENA_BUFFER(arena, arena->pos);
        buf = arena->pos;
        arena->pos = push_to;

        /* handle committing */
        if (arena->pos >= arena->commit_pos)
        {
            #ifdef MEM_ARENA_USE_RESERVE_AND_COMMIT_STRATEGY
              int committed = MEM_ARENA_OS_COMMIT(arena->commit_pos, size);
              MEM_ARENA_ASSERT(committed);
            #endif

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
void mem_arena_pop_to(mem_arena_t* arena, char* buf) {
    MEM_ARENA_ASSERT((char*) (arena + sizeof(mem_arena_t)) <= buf);
    MEM_ARENA_ASSERT(arena->end >= buf);

    //size_t new_pos =  (unsigned char*) buf - (unsigned char*) ARENA_BUFFER(arena, 0);
    size_t diff = arena->pos - buf;
    if (diff > 0)
    {
        arena->pos  = buf;
        //arena->pos = new_pos;
        memset(arena->pos, 0, diff);

        /* TODO maybe handle decommitting here */
    }
}
void mem_arena_pop_by(mem_arena_t* arena, size_t bytes) {
    MEM_ARENA_ASSERT((arena->pos - bytes) >= ((char*) arena + sizeof(mem_arena_t)));
    mem_arena_pop_to(arena, arena->pos - bytes);
}
void mem_arena_clear(mem_arena_t*  arena) {
    /* NOTE: cannot be called with scratch arenas */
    arena->pos = (char*) arena;

    /* TODO maybe handle decommitting here */
}
void mem_arena_destroy(mem_arena_t** arena) {
    size_t cap = (*arena)->end - (char*) (*arena);

    #ifdef MEM_ARENA_USE_RESERVE_AND_COMMIT_STRATEGY
      MEM_ARENA_OS_DECOMMIT((void*) *arena, cap);
      MEM_ARENA_OS_RELEASE((void*) *arena, cap);
    #else
      MEM_ARENA_OS_FREE((void*) *arena);
    #endif

    *arena = NULL;
}

#define ARENA_DEFAULT_RESERVE_SIZE (4 * 1024 * 1024)
mem_arena_t* mem_arena_default() {
    mem_arena_t* default_arena = mem_arena_create(ARENA_DEFAULT_RESERVE_SIZE);
    return default_arena;
}
#endif // MEM_ARENA_IMPLEMENTATION
