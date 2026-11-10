#include "xdf.h"

#include <stddef.h>
#include <stdint.h>

int ss_xdf_read_sector(void* context, uint32_t sector, uint8_t* out) {
    SSXdfReader* reader = context;
    if (!reader || !reader->read || !out || ((uintptr_t)out & 1u) ||
        reader->drive > 3 || sector >= SS_XDF_SECTORS)
        return -1;
    uint32_t track = sector / 16u;
    uint32_t side = (sector / 8u) % 2u;
    uint32_t number = sector % 8u + 1u;
    uint32_t position = 0x03000000u | (track << 16) | (side << 8) | number;
    int mode = (int)(((uint32_t)0x90u + reader->drive) << 8) | 0x70;
    uint32_t status = (uint32_t)reader->read(mode, (int)position, 1024, out);
    /* ST0's unit/head bits and the returned cylinder are not errors. */
    return (status & 0xf8ffff00u) == 0 ? 0 : -1;
}
