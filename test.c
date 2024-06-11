#define MEMORY_IMPLEMENTATION
#include "memory.h"
#define MEM_ARENA_IMPLEMENTATION
#include "mem_arena.h"

#include "mem_pool.h"

typedef struct thing_t {
    int foo;
    float bar;
} thing_t;

#ifdef _WIN32
  #include <windows.h>
  #define SLEEP(x) Sleep(x*1000)
#else
  #include <unistd.h>
  #define SLEEP(x) sleep(x)
#endif

int main()
{
    #define THING_COUNT 10
    mem_arena_t* arena = mem_arena_create(256);

    mem_pool_t* thing_pool = mem_pool_create(arena, thing_t, THING_COUNT);

    for (int i = 0; i < THING_COUNT; i++)
    {
        thing_t* thing = mem_pool_alloc(thing_pool, thing_t);
        SLEEP(1);
        thing->foo = 2;
        thing->bar = 4.2f;
        //printf("%zu\n", sizeof(thing_t));
        printf("%i %f\n", thing->foo, thing->bar);
    }

    return 1;
}
