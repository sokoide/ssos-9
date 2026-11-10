#ifndef SS_XDF_H
#define SS_XDF_H

#include <stdint.h>

#define SS_XDF_SECTORS 1232u

/* Same argument order as IOCS _B_READ. */
typedef int (*SSXdfIocsRead)(int mode, int position, int size, void* out);
typedef struct {
    SSXdfIocsRead read;
    uint8_t drive;
} SSXdfReader;

/* Reads one 1024-byte 2HD sector; compatible with SSFat12ReadSector. */
int ss_xdf_read_sector(void* context, uint32_t sector, uint8_t* out);

#endif
