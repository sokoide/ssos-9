#ifndef SS_FAT12_H
#define SS_FAT12_H

#include <stdint.h>

/* Read-only FAT12/FAT16 volume reader. The block reader reads one 512- or
 * 1024-byte logical sector and returns 0 on success. Buffers supplied to it
 * have room for 1024 bytes. Existing names are retained for callers. */
typedef int (*SSFat12ReadSector)(void* context, uint32_t sector, uint8_t* out);
typedef int (*SSFatWriteSector)(void* context, uint32_t sector, const uint8_t* in);

typedef struct {
    SSFat12ReadSector read_sector;
    void* context;
    uint32_t total_sectors;
    uint32_t fat_start;
    uint32_t root_start;
    uint32_t data_start;
    uint16_t fat_sectors;
    uint16_t root_entries;
    uint16_t cluster_count;
    uint16_t sector_size;
    uint8_t sectors_per_cluster;
    uint8_t fat_bits; /* 12 or 16; determined by the BPB cluster count. */
    uint8_t fat_count;
} SSFat12;

typedef struct {
    SSFat12* volume;
    uint32_t size;
    uint32_t offset;
    uint16_t cursor_cluster;
    uint16_t cursor_index;
} SSFat12File;

int ss_fat12_mount(SSFat12* volume, SSFat12ReadSector read_sector,
                   void* context, uint32_t available_sectors);
int ss_fat12_open(SSFat12* volume, const char* name, SSFat12File* file);
int32_t ss_fat12_read(void* context, uint8_t* out, uint32_t size);

/* Create a new root-directory 8.3 file on FAT16. The caller serializes all
 * access to the volume. Existing names are rejected. A failed metadata write
 * may leave allocated clusters or inconsistent FAT copies: the device has no
 * transactional write guarantee and must be checked before reuse. */
int ss_fat16_create(SSFat12* volume, SSFatWriteSector write_sector,
                    const char* name, const uint8_t* data, uint32_t size);
/* Replace an existing regular root file using a newly allocated chain. The
 * old chain is released only after the new directory entry is written. */
int ss_fat16_replace(SSFat12* volume, SSFatWriteSector write_sector,
                     const char* name, const uint8_t* data, uint32_t size);

#endif
