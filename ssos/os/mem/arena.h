#ifndef SS_ARENA_H
#define SS_ARENA_H

#include <stdint.h>

typedef struct SSArenaBlock SSArenaBlock;
struct SSArenaBlock {
    uint32_t size;
    SSArenaBlock* next;
    uint32_t magic;
};

typedef struct {
    uintptr_t start;
    uintptr_t end;
    SSArenaBlock* free_list;
} SSArena;

int16_t ss_arena_init(SSArena* arena, void* memory, uint32_t size);
void* ss_arena_alloc(SSArena* arena, uint32_t size);
int16_t ss_arena_free(SSArena* arena, void* memory);
uint32_t ss_arena_free_bytes(const SSArena* arena);
uint32_t ss_arena_largest_free(const SSArena* arena);

#endif
