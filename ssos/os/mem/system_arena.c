#include "system_arena.h"

#include "arena.h"
#include "kernel.h"

#ifdef LOCAL_MODE
#ifndef SS_X_ARENA_BYTES
#define SS_X_ARENA_BYTES 262144
#endif
static uint8_t local_arena[SS_X_ARENA_BYTES] __attribute__((aligned(4)));
#else
extern uint8_t __ssosram_start;
extern uint8_t __ssosram_end;
#endif

static SSArena system_arena;

int16_t ss_memory_init(void) {
#ifdef LOCAL_MODE
    void* start = local_arena;
    uint32_t size = sizeof(local_arena);
#else
    void* start = &__ssosram_start;
    uintptr_t end = (uintptr_t)&__ssosram_end;
    uintptr_t base = (uintptr_t)start;
    if (end <= base || end - base > UINT32_MAX)
        return SS_ERR_STATE;
    uint32_t size = (uint32_t)(end - base);
#endif
    return ss_arena_init(&system_arena, start, size) == 0 ? SS_OK
                                                          : SS_ERR_STATE;
}

void* ss_memory_alloc(uint32_t size) {
    uint16_t saved_sr = ss_irq_save();
    void* memory = ss_arena_alloc(&system_arena, size);
    ss_irq_restore(saved_sr);
    return memory;
}

int16_t ss_memory_free(void* memory) {
    uint16_t saved_sr = ss_irq_save();
    int16_t status =
        ss_arena_free(&system_arena, memory) == 0 ? SS_OK : SS_ERR_PARAM;
    ss_irq_restore(saved_sr);
    return status;
}

uint32_t ss_memory_free_bytes(void) {
    uint16_t saved_sr = ss_irq_save();
    uint32_t bytes = ss_arena_free_bytes(&system_arena);
    ss_irq_restore(saved_sr);
    return bytes;
}

uint32_t ss_memory_largest_free(void) {
    uint16_t saved_sr = ss_irq_save();
    uint32_t bytes = ss_arena_largest_free(&system_arena);
    ss_irq_restore(saved_sr);
    return bytes;
}
