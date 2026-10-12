#ifndef SS_SYSTEM_ARENA_H
#define SS_SYSTEM_ARENA_H

#include <stdint.h>

int16_t ss_memory_init(void);
void* ss_memory_alloc(uint32_t size);
int16_t ss_memory_free(void* memory);
uint32_t ss_memory_free_bytes(void);
uint32_t ss_memory_largest_free(void);

#endif
