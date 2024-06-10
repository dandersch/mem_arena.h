#pragma once

#ifndef MEM_ASSERT
    #include <assert.h>
    #define MEM_ASSERT(expr) assert(expr)
#endif

#include <stddef.h> // for size_t
#include <stdint.h> // for uintptr_t

/* memory is guaranteed to be initialized to zero */
void*  mem_reserve (void* at,    size_t size);  /* pass NULL if memory location doesn't matter */
int    mem_commit  (void* ptr,   size_t size);
void*  mem_alloc   (size_t size);               /* wraps malloc() */
int    mem_decommit(void* ptr,   size_t size);
void   mem_release (void* ptr,   size_t size);
void   mem_free    (void* ptr);                 /* can only be called with memory from mem_alloc */
void   mem_zero_out(void* ptr,   size_t size);
int    mem_equal   (void* buf_a, void* buf_b, size_t size_in_bytes);
void   mem_copy    (void* dst,   void* src,   size_t size_in_bytes);
size_t mem_pagesize(); /* pagesize in bytes */

/* helper macros */
#define MEM_ZERO_OUT_STRUCT(s) mem_zero_out((s), sizeof(*(s)))
#define MEM_ZERO_OUT_ARRAY(a)  MEM_ASSERT(IS_ARRAY(a)); mem_zero_out((a), sizeof(a))

#define KILOBYTES(val) (         (val) * 1024LL)
#define MEGABYTES(val) (KILOBYTES(val) * 1024LL)
#define GIGABYTES(val) (MEGABYTES(val) * 1024LL)
#define TERABYTES(val) (GIGABYTES(val) * 1024LL)

/* alignment / power-of-2 macros */
#define CHECK_IF_POW2(x)         (((x)&((x)-1))==0)
// some bit hacking to get the next alignment value.
// e.g.: NEXT_ALIGN_POW2(12,16) == 16, NEXT_ALIGN_POW2(18,16) == 32
#define NEXT_ALIGN_POW2(x,align) (((x) + (align) - 1) & ~((align) - 1))
#define PREV_ALIGN_POW2(x,align) ((x) & ~((align) - 1))

/* align e.g. a memory address to its next page boundary */
#define ALIGN_TO_NEXT_PAGE(val) NEXT_ALIGN_POW2((uintptr_t) val, mem_pagesize())
#define ALIGN_TO_PREV_PAGE(val) PREV_ALIGN_POW2((uintptr_t) val, mem_pagesize())
/* NOTE mem_pagesize() is called for every commit, could be hardcoded to 4096 */

#ifdef MEMORY_IMPLEMENTATION

/* NOTE: alloc & free are the same for all platforms and just wrap malloc for now (but the memory is zeroed out) */
#include <stdlib.h> // for malloc
void* mem_alloc(size_t size) { void* mem = malloc(size); mem_zero_out(mem, size); return mem; }
void  mem_free(void* ptr) { free(ptr); }

#if defined(_WIN32)
#include <windows.h>
void* mem_reserve(void* at, size_t size) {
    void* mem = VirtualAlloc(at, size, MEM_RESERVE, PAGE_READWRITE);
    return mem;
}
int mem_commit(void* ptr, size_t size) {
    int result = (VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE) != 0);
    return result;
}
int mem_decommit(void* ptr, size_t size) {
    return VirtualFree(ptr, size, MEM_DECOMMIT);
}
void mem_release(void* ptr,  size_t size) {
    VirtualFree(ptr, 0, MEM_RELEASE);
}
void mem_zero_out(void* ptr, size_t size) {
    SecureZeroMemory(ptr, size);
}
int mem_equal(void* buf_a, void* buf_b, size_t size_in_bytes) {
    /* NOTE RtlCompareMemory works differently from memcmp (it returns number
     * of bytes matched), so we limit our memory compare function to just test
     * for equality right now */
    return (RtlCompareMemory(buf_a, buf_b, size_in_bytes) == size_in_bytes);
}
void mem_copy(void* dst, void* src, size_t size_in_bytes) {
    RtlCopyMemory(dst, src, size_in_bytes);
}
size_t  mem_pagesize() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
}

#elif defined(__linux__)

#include <string.h>   /* for memset, memcpy, memcmp */
#include <sys/mman.h> /* for mmmap, mprotect, madvise */
#include <unistd.h>   /* for getpagesize() */
#include <errno.h>    /* TODO only for debugging */

/*
 * NOTE: right now we only use mmap & mprotect for reserving & committing memory
 * and no malloc(), because it seems that we can't easily mix e.g. calls like
 * void* buf=malloc w/ subsequent calls to munmap(buf) or calls to buf = mmap
 * with subsequent calls to free(buf).
 *
 * Maybe we just use malloc() in mem_alloc() and leave it to the user to not
 * decommit malloc'ed memory or free() reserved memory
 *
 * TODO Is there a way to check if memory was allocated by malloc?
 *
 */

// see http://www.smallbulb.net/2018/809-reserve-virtual-memory:
// to reserve on linux, PROT_NONE tells kernel there should be no mapping to real memory:
// void* ptr = mmap(nullptr, 128ul * 1024 * 1024 * 1024, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
// mprotect(ptr, 5 * 1024 * 1024, PROT_READ | PROT_WRITE); // commit 5 MB
void* mem_reserve(void* at, size_t size) {
    /* try to get a fixed memory address if passed in. User has to check if at==mem to see if it worked */
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    if (at) { flags |= MAP_FIXED; }
    void* mem = mmap(at, size, PROT_NONE, flags, -1, 0);
    if (mem == MAP_FAILED) { mem = NULL; }
    return mem;
}
int mem_commit(void* ptr, size_t size) {
    /* TODO mprotect fails if addr is not aligned to a page boundary and if
     * length (size) is not a multiple of the page size as returned by sysconf(). */
    uintptr_t commit_begin = (uintptr_t) ptr;
    uintptr_t commit_end   = (uintptr_t) ptr + size;
    commit_begin           = ALIGN_TO_PREV_PAGE(commit_begin);
    commit_end             = ALIGN_TO_NEXT_PAGE(commit_end);
    size                   = commit_end - commit_begin;
    ptr                    = (void*) commit_begin;

    size += mem_pagesize(); /* TODO: overcommitting here by 1 pagesize solves some edge cases - find out why */

    MEM_ASSERT(!(size % mem_pagesize()));
    MEM_ASSERT(!(((uintptr_t) ptr) % mem_pagesize()));

    int i = mprotect(ptr, size, PROT_READ | PROT_WRITE);
    MEM_ASSERT(i == 0); // we assert here for now, should be done by the user
    return (i == 0);
}
int mem_decommit(void* ptr, size_t size) {
    int result     = mprotect(ptr, size, PROT_NONE);
    /* NOTE: This doesn't seem to do anything and is not supported by all compilers */
    //int result = madvise(ptr, size, MADV_DONTNEED);
    return (result == 0);
}
void mem_release(void* ptr,  size_t size) {
    munmap(ptr, size);
}
void mem_zero_out(void* ptr, size_t size) {
    memset(ptr, 0, size);
}
int mem_equal(void* buf_a, void* buf_b, size_t size_in_bytes) {
    return (memcmp(buf_a, buf_b, size_in_bytes) == 0);
}
void mem_copy(void* dst, void* src, size_t size_in_bytes) {
    memcpy(dst, src, size_in_bytes);
}
size_t  mem_pagesize() {
    return sysconf(_SC_PAGE_SIZE);
}
#endif
#endif // MEMORY_IMPLEMENTATION
