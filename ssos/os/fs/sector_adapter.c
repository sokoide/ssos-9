#include "sector_adapter.h"

#include <stddef.h>

int ss_sector_read512(void* context, uint32_t sector, uint8_t* out) {
    SSSectorAdapter* adapter = context;
    uint8_t physical[1024] __attribute__((aligned(4)));
    if (!adapter || !adapter->read || !out || sector / 2u >= adapter->sectors)
        return -1;
    if (adapter->read(adapter->context, sector / 2u, physical) != 0)
        return -1;
    uint32_t start = (sector & 1u) * 512u;
    for (uint32_t i = 0; i < 512; i++) out[i] = physical[start + i];
    return 0;
}
