/* malloc(), realloc() and free() on a fixed arena in the blob's bss. The
 * patcher appends the blob last and gives the payload section a virtual
 * size beyond its file data, so the arena takes no space in the DLL and
 * the loader zero-fills it.
 *
 * The arena is a sequence of blocks, each a header followed by its bytes.
 * Allocation takes the first free block that fits, merging free neighbours
 * on the way and splitting off the rest; free() only marks a block free.
 * That suits the few, short-lived allocations of loading a script.
 */
#include "blob.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef BLOB_ARENA_SIZE
#define BLOB_ARENA_SIZE (4u << 20)
#endif

/* Doubles and 64-bit integers want 8-byte alignment. */
#define ALIGN 8u

typedef struct {
    uint32_t size;  /* bytes after the header, a multiple of ALIGN */
    uint32_t used;
} header_t;

_Static_assert(sizeof(header_t) % ALIGN == 0, "headers keep blocks aligned");
_Static_assert(BLOB_ARENA_SIZE % ALIGN == 0, "the arena holds whole blocks");

static unsigned char arena[BLOB_ARENA_SIZE] __attribute__((aligned(ALIGN)));

static header_t *after(header_t *block)
{
    return (header_t *)((unsigned char *)(block + 1) + block->size);
}

static header_t *end(void)
{
    return (header_t *)(arena + sizeof(arena));
}

/* Absorb the free blocks that directly follow block into it. */
static void merge(header_t *block)
{
    for (header_t *next = after(block); next < end() && !next->used; next = after(block))
        block->size += (uint32_t)sizeof(header_t) + next->size;
}

/* Keep size bytes of block and turn a large enough rest into a free block. */
static void split(header_t *block, uint32_t size)
{
    if (block->size - size < sizeof(header_t) + ALIGN)
        return;
    header_t *rest = (header_t *)((unsigned char *)(block + 1) + size);
    rest->size = block->size - size - (uint32_t)sizeof(header_t);
    rest->used = 0;
    block->size = size;
}

/* The block size for a request, or 0 if it can never fit. */
static uint32_t rounded(size_t size)
{
    if (size > sizeof(arena) - sizeof(header_t))
        return 0;
    return (uint32_t)((size + ALIGN - 1) & ~(size_t)(ALIGN - 1));
}

void *malloc(size_t size)
{
    header_t *first = (header_t *)arena;
    /* The zero-filled arena starts as one free block. */
    if (!first->size)
        first->size = (uint32_t)(sizeof(arena) - sizeof(header_t));
    uint32_t need = rounded(size ? size : 1);
    if (!need)
        return NULL;
    for (header_t *block = first; block < end(); block = after(block)) {
        if (block->used)
            continue;
        merge(block);
        if (block->size >= need) {
            split(block, need);
            block->used = 1;
            return block + 1;
        }
    }
    return NULL;
}

void free(void *pointer)
{
    if (pointer)
        ((header_t *)pointer - 1)->used = 0;
}

void *realloc(void *pointer, size_t size)
{
    if (!pointer)
        return malloc(size);
    header_t *block = (header_t *)pointer - 1;
    uint32_t need = rounded(size ? size : 1);
    if (!need)
        return NULL;
    /* Grow into the free blocks behind, or shrink, in place. */
    merge(block);
    if (block->size >= need) {
        split(block, need);
        return pointer;
    }
    void *moved = malloc(size);
    if (moved) {
        memcpy(moved, pointer, block->size);
        free(pointer);
    }
    return moved;
}
