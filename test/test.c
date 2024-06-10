//#if defined(_WIN32)
//  #include <windows.h>
//  #define MEM_ARENA_OS_RESERVE(size)      VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_READWRITE)
//  #define MEM_ARENA_OS_COMMIT(ptr,size)   (VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE) != 0)
//  #define MEM_ARENA_OS_DECOMMIT(ptr,size) VirtualFree(ptr, size, MEM_DECOMMIT);
//  #define MEM_ARENA_OS_RELEASE(ptr,size)  VirtualFree(ptr, 0, MEM_RELEASE);
//#elif defined(__linux__)
//  #include <sys/mman.h> /* for mmmap, mprotect, madvise */
//  #define MEM_ARENA_OS_RESERVE(size)      mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
//  /* TODO mprotect fails if addr is not aligned to pageboundary and if length (size) is not multiple of pagesize (given by sysconf()). */
//  #define MEM_ARENA_OS_COMMIT(ptr,size)   mprotect(ptr, size, PROT_READ | PROT_WRITE);
//  #define MEM_ARENA_OS_DECOMMIT(ptr,size) mprotect(ptr, size, PROT_NONE);
//  #define MEM_ARENA_OS_RELEASE(ptr,size)  munmap(ptr, size)
//#endif
#define MEMORY_IMPLEMENTATION
#include "../memory.h"

#define MEM_ARENA_IMPLEMENTATION
#define MEM_ARENA_OS_RESERVE(size)      mem_reserve(NULL, size)
#define MEM_ARENA_OS_COMMIT(ptr,size)   mem_commit(ptr, size)
#define MEM_ARENA_OS_RELEASE(ptr,size)  mem_release(ptr, size)
#define MEM_ARENA_OS_DECOMMIT(ptr,size) mem_decommit(ptr, size)
#include "../mem_arena.h"


#define KILOBYTES(val) (         (val) * 1024LL)
#define MEGABYTES(val) (KILOBYTES(val) * 1024LL)
#define GIGABYTES(val) (MEGABYTES(val) * 1024LL)
#define TERABYTES(val) (GIGABYTES(val) * 1024LL)

#define RES_MEM_ENTITIES    GIGABYTES(1)
#define RES_MEM_GAME_TEMP   MEGABYTES(100)
#define RES_MEM_GAME        RES_MEM_ENTITIES + RES_MEM_GAME_TEMP
#define RES_MEM_TEXTURES    MEGABYTES(100)
#define RES_MEM_MESHES      MEGABYTES(100)
#define RES_MEM_RENDERER    RES_MEM_TEXTURES + RES_MEM_MESHES
#define RES_MEM_FILES       MEGABYTES(512)
#define RES_MEM_NETWORK     KILOBYTES(10)
#define RES_MEM_PLATFORM    RES_MEM_FILES + RES_MEM_NETWORK
#define RES_MEM_APPLICATION RES_MEM_GAME + RES_MEM_RENDERER + RES_MEM_PLATFORM

#include <stdio.h>
int main(int argc, char** argv)
{
    /* TEST MEMORY ALLOCATION */
    {
        unsigned int buf_size_reserved  = MEGABYTES(8);

        void* fixed_address = NULL;
        #if defined(BUILD_DEBUG) && defined(ARCH_X64)
        fixed_address = (void*) GIGABYTES(256); // 0x4000000000
        #endif
        unsigned char* buf       = (unsigned char*) mem_reserve(fixed_address, buf_size_reserved);
        assert(buf);
        #if defined(BUILD_DEBUG) && defined(ARCH_X64)
        ASSERT(buf == fixed_address);
        #endif

        size_t buf_size_committed = KILOBYTES(12);
        int committed          = mem_commit(buf, buf_size_committed);
        assert(committed);

        unsigned char* buf_2   = (unsigned char*) mem_alloc(buf_size_committed);
        assert(buf_2);

        /* test if memory is initialized to zero */
        for (size_t i = 0; i < buf_size_committed; i++)
        {
            assert(!buf[i]);
            assert(!buf_2[i]);
        }

        /* copying and comparing memory */
        for (size_t i = 0; i < buf_size_committed; i++)
        {
            buf[i] = 'a';
        }
        assert(!mem_equal(buf, buf_2, buf_size_committed));
        mem_copy(buf_2, buf, buf_size_committed);
        assert(mem_equal(buf, buf_2, buf_size_committed));

        /* freeing memory */
        int decommitted = mem_decommit(buf, buf_size_committed);
        assert(decommitted);
        // for (u32 i = 0; i < buf_size_committed; i++) { buf[0] = 'b'; } // should cause SEGV
        mem_release(buf,  buf_size_committed);
        mem_free(buf_2,   buf_size_committed);
    }

    /* TEST ARENAS */
    {
        mem_arena_t* arena = mem_arena_create(MEGABYTES(1));
        unsigned char* arena_buf     = (unsigned char*) mem_arena_push(arena, KILOBYTES(4));
        assert(arena);
        assert(arena_buf);
        for (size_t i = 0; i < KILOBYTES(4); i++) { assert(!arena_buf[i]); }
        mem_arena_pop_by(arena, KILOBYTES(1));
        assert(mem_arena_get_pos(arena) == KILOBYTES(3));

        /* test if memory after popping & pushing is still zeroed */
        mem_arena_push(arena, KILOBYTES(1));
        for (size_t i = 0; i < KILOBYTES(4); i++) { assert(!arena_buf[i]); }

        struct test_align_unpacked
        {
            int   a; //    4B
            char  b; // +  1B
            float c; // +  4B
                     // = 12B bc of std alignment
        };
        struct test_align_unpacked* struct_test = ARENA_PUSH_STRUCT(arena, struct test_align_unpacked);
        size_t* number_arr = ARENA_PUSH_ARRAY(arena, size_t, 256);
        for (size_t i = 0; i < 256; i++) { assert(!number_arr[i]); }

        mem_arena_pop_to(arena, number_arr);
        number_arr = ARENA_PUSH_ARRAY(arena, size_t, 256);
        for (size_t i = 0; i < 256; i++) { assert(!number_arr[i]); }

        /* provoke an overflow */
        //mem_arena_push(&sub_arena, KILOBYTES(3));
        //mem_arena_push(&arena,     MEGABYTES(10));
    }

    /* TEST ARENA RESERVING & COMMITTING */
    {
        mem_arena_t* arena   = mem_arena_create(KILOBYTES(32));
        unsigned char* arena_buf_1      = (unsigned char*) mem_arena_push(arena, KILOBYTES(4));
        assert(arena_buf_1);
        for (size_t i = 0; i < KILOBYTES(4); i++) { assert(!arena_buf_1[i]); }

        unsigned char* arena_buf_2      = (unsigned char*) mem_arena_push(arena, KILOBYTES(16));
        assert(arena_buf_2);
        for (size_t i = 0; i < KILOBYTES(16); i++) { assert(!arena_buf_2[i]); }
    }

    /* TEST SUBARENAS */
    {
        mem_arena_t* base_arena     = mem_arena_create(RES_MEM_APPLICATION);
        mem_arena_t* platform_arena = mem_arena_subarena(base_arena, RES_MEM_PLATFORM);
        assert(platform_arena);
        mem_arena_t* renderer_arena = mem_arena_subarena(base_arena, RES_MEM_RENDERER);
        assert(renderer_arena);
        mem_arena_t* game_arena     = mem_arena_subarena(base_arena, RES_MEM_GAME);
        assert(game_arena);

        // TODO find a way to turn overallocating the base arena into a compile time error
        //mem_arena_t* test_arena     = mem_arena_subarena(base_arena, 1); // should fail

        //u8* test_buf = (u8*) mem_arena_push(game_arena, KILOBYTES(5)); // push beyond pagesize
        //for (u32 i = 0; i < KILOBYTES(8); i++) { test_buf[i] = 'a'; }
    }

    return 0;
}

