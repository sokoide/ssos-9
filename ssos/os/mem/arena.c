#include "arena.h"

#include <stddef.h>

#define SS_ARENA_FREE_MAGIC 0x46524545u
#define SS_ARENA_USED_MAGIC 0x55534544u
#define SS_ARENA_ALIGNMENT 4u

static uint32_t block_header_size(void) {
    return (uint32_t)((sizeof(SSArenaBlock) + SS_ARENA_ALIGNMENT - 1u) &
                      ~(SS_ARENA_ALIGNMENT - 1u));
}

static uint32_t align_size(uint32_t size) {
    if (size > UINT32_MAX - (SS_ARENA_ALIGNMENT - 1u))
        return 0;
    return (size + SS_ARENA_ALIGNMENT - 1u) & ~(SS_ARENA_ALIGNMENT - 1u);
}

int16_t ss_arena_init(SSArena* arena, void* memory, uint32_t size) {
    if (arena == NULL || memory == NULL)
        return -1;

    uintptr_t raw_start = (uintptr_t)memory;
    if (raw_start > UINTPTR_MAX - (SS_ARENA_ALIGNMENT - 1u) ||
        raw_start > UINTPTR_MAX - size)
        return -1;
    uintptr_t start = (raw_start + SS_ARENA_ALIGNMENT - 1u) & ~(uintptr_t)3u;
    uintptr_t end = (raw_start + size) & ~(uintptr_t)3u;
    uint32_t header_size = block_header_size();
    if (end < start || end - start < header_size + SS_ARENA_ALIGNMENT)
        return -1;

    arena->start = start;
    arena->end = end;
    arena->free_list = (SSArenaBlock*)start;
    arena->free_list->size = (uint32_t)(end - start);
    arena->free_list->next = NULL;
    arena->free_list->magic = SS_ARENA_FREE_MAGIC;
    return 0;
}

void* ss_arena_alloc(SSArena* arena, uint32_t size) {
    if (arena == NULL || arena->free_list == NULL || size == 0)
        return NULL;

    uint32_t payload_size = align_size(size);
    uint32_t header_size = block_header_size();
    if (payload_size == 0 || payload_size > UINT32_MAX - header_size)
        return NULL;
    uint32_t needed = header_size + payload_size;

    SSArenaBlock** link = &arena->free_list;
    while (*link != NULL && (*link)->size < needed) link = &(*link)->next;
    SSArenaBlock* block = *link;
    if (block == NULL)
        return NULL;

    uint32_t remainder = block->size - needed;
    if (remainder >= header_size + SS_ARENA_ALIGNMENT) {
        SSArenaBlock* split = (SSArenaBlock*)((uintptr_t)block + needed);
        split->size = remainder;
        split->next = block->next;
        split->magic = SS_ARENA_FREE_MAGIC;
        *link = split;
        block->size = needed;
    } else {
        *link = block->next;
    }

    block->next = NULL;
    block->magic = SS_ARENA_USED_MAGIC;
    return (void*)((uintptr_t)block + header_size);
}

int16_t ss_arena_free(SSArena* arena, void* memory) {
    if (arena == NULL || memory == NULL || arena->free_list == NULL)
        return -1;

    uint32_t header_size = block_header_size();
    uintptr_t payload = (uintptr_t)memory;
    if (payload < arena->start + header_size || payload >= arena->end)
        return -1;
    SSArenaBlock* block = (SSArenaBlock*)(payload - header_size);
    uintptr_t block_start = (uintptr_t)block;
    if (block_start < arena->start || block_start + header_size > arena->end ||
        block->magic != SS_ARENA_USED_MAGIC ||
        block->size < header_size + SS_ARENA_ALIGNMENT ||
        block->size > arena->end - block_start)
        return -1;

    SSArenaBlock** link = &arena->free_list;
    SSArenaBlock* previous = NULL;
    while (*link != NULL && (uintptr_t)*link < block_start) {
        previous = *link;
        link = &(*link)->next;
    }
    SSArenaBlock* next = *link;
    if ((previous != NULL &&
         (uintptr_t)previous + previous->size > block_start) ||
        (next != NULL && block_start + block->size > (uintptr_t)next))
        return -1;

    block->magic = SS_ARENA_FREE_MAGIC;
    block->next = next;
    *link = block;

    if (previous != NULL &&
        (uintptr_t)previous + previous->size == block_start) {
        previous->size += block->size;
        previous->next = block->next;
        block = previous;
    }
    if (block->next != NULL &&
        (uintptr_t)block + block->size == (uintptr_t)block->next) {
        block->size += block->next->size;
        block->next = block->next->next;
    }
    return 0;
}

uint32_t ss_arena_free_bytes(const SSArena* arena) {
    if (arena == NULL)
        return 0;
    uint32_t result = 0;
    uint32_t header_size = block_header_size();
    for (SSArenaBlock* block = arena->free_list; block != NULL;
         block = block->next) {
        result += block->size - header_size;
    }
    return result;
}

uint32_t ss_arena_largest_free(const SSArena* arena) {
    if (arena == NULL)
        return 0;
    uint32_t largest = 0;
    uint32_t header_size = block_header_size();
    for (SSArenaBlock* block = arena->free_list; block != NULL;
         block = block->next) {
        uint32_t available = block->size - header_size;
        if (available > largest)
            largest = available;
    }
    return largest;
}
