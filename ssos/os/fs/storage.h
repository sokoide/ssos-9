#ifndef SS_STORAGE_H
#define SS_STORAGE_H

#include "fat12.h"

typedef struct {
    SSFat12 volume;
    SSFatWriteSector write_sector;
    uint8_t mounted;
    uint8_t write_failed;
} SSStorage;

/* Callers serialize mount and all file operations on a given device. The
 * physical block size must match the FAT logical sector size. */
int ss_storage_mount(SSStorage* storage, SSFat12ReadSector read,
                     SSFatWriteSector write, void* context,
                     uint32_t sectors, uint16_t block_size);
void ss_storage_unmount(SSStorage* storage);
/* replace=0 creates only, replace=1 replaces only. An I/O error or a failure
 * after a write was attempted fences subsequent writes until unmount and
 * external volume inspection. */
int ss_storage_store(SSStorage* storage, const char* name, const uint8_t* data,
                     uint32_t size, int replace);
int32_t ss_storage_read(SSStorage* storage, const char* name, uint8_t* data,
                        uint32_t capacity);
int32_t ss_system_file_load(const char* name, uint8_t* data, uint32_t capacity);
int32_t ss_system_file_store(const char* name, const uint8_t* data,
                             uint32_t size, uint32_t replace);

#endif
