#ifndef SS_SECTOR_ADAPTER_H
#define SS_SECTOR_ADAPTER_H

#include <stdint.h>

/* Read a 1024-byte physical sector by linear index. */
typedef int (*SSRead1024)(void* context, uint32_t sector, uint8_t* out);

typedef struct {
    SSRead1024 read;
    void* context;
    uint32_t sectors;
} SSSectorAdapter;

/* SSFat12ReadSector-compatible adapter for 512-byte logical sectors. */
int ss_sector_read512(void* context, uint32_t sector, uint8_t* out);

#endif
