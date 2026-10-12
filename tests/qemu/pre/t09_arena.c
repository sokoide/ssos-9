/* Exercise the production first-fit arena and coalescing code under QEMU. */

#include "arena.h"
#include "tty.h"

#include <stddef.h>

static uint8_t memory[512] __attribute__((aligned(4)));
static SSArena arena;

static void fail(const char* reason) {
    tty_puts("FAIL ");
    tty_puts(reason);
    tty_puts("\n");
    for (;;) {
    }
}

int main(void) {
    tty_puts("START arena\n");
    if (ss_arena_init(&arena, memory, sizeof(memory)) != 0)
        fail("init");
    uint32_t initial = ss_arena_free_bytes(&arena);
    if (initial == 0 || ss_arena_largest_free(&arena) != initial)
        fail("initial stats");

    void* first = ss_arena_alloc(&arena, 31);
    void* middle = ss_arena_alloc(&arena, 64);
    void* last = ss_arena_alloc(&arena, 80);
    if (first == NULL || middle == NULL || last == NULL)
        fail("allocations");
    if (((uintptr_t)first & 3u) != 0 || ((uintptr_t)middle & 3u) != 0 ||
        ((uintptr_t)last & 3u) != 0 ||
        !((uintptr_t)first < (uintptr_t)middle &&
          (uintptr_t)middle < (uintptr_t)last))
        fail("alignment/order");
    if (ss_arena_alloc(&arena, 4096) != NULL)
        fail("exhaustion");

    if (ss_arena_free(&arena, middle) != 0)
        fail("free middle");
    void* reused = ss_arena_alloc(&arena, 32);
    if (reused != middle)
        fail("first fit reuse");
    if (ss_arena_free(&arena, reused) != 0 ||
        ss_arena_free(&arena, reused) == 0)
        fail("double free");
    if (ss_arena_free(&arena, first) != 0 || ss_arena_free(&arena, last) != 0)
        fail("free all");
    if (ss_arena_free_bytes(&arena) != initial ||
        ss_arena_largest_free(&arena) != initial)
        fail("coalescing");

    tty_puts("OK arena split/coalesce\n");
    for (;;) {
    }
}
