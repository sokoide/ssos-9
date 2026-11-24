#ifndef SS_STORAGE_H
#define SS_STORAGE_H

#include "fat12.h"

typedef struct {
    SSFat12 volume;
    SSFatWriteSector write_sector;
    uint8_t mounted;
    uint8_t write_failed;
    uint8_t readers;
    uint8_t writer;
} SSStorage;

/* Callers serialize all file operations on a given device. Only
 * ss_scsi_volume_attach creates a mounted SSStorage (a Human68k-format
 * FAT16 volume; FAT2 is reserved and unused, writes update FAT1 alone).
 * Write failures fence subsequent writes until SSOS-9 restarts and the
 * volume is inspected; a partially completed update is not atomic. */
int ss_storage_store(SSStorage* storage, const char* name, const uint8_t* data,
                     uint32_t size, int replace);
/* One-shot read of a whole file. Mirrors the store/open_reader exclusivity:
 * fails while a writer transaction is active or the write fence is set. */
int32_t ss_storage_read(SSStorage* storage, const char* name, uint8_t* data,
                        uint32_t capacity);
int ss_storage_open_reader(SSStorage* storage, const char* name, SSFat12File* file);
void ss_storage_close_reader(SSStorage* storage, SSFat12File* file);
int ss_storage_writer_begin(SSStorage* storage, const char* name, int replace,
                            SSFat16Writer* writer);
int32_t ss_storage_writer_write(SSStorage* storage, SSFat16Writer* writer,
                                const uint8_t* data, uint32_t size);
int ss_storage_writer_end(SSStorage* storage, SSFat16Writer* writer, int commit);
int32_t ss_system_file_load(const char* name, uint8_t* data, uint32_t capacity);
int32_t ss_system_file_store(const char* name, const uint8_t* data,
                             uint32_t size, uint32_t replace);
int32_t ss_system_open_reader(uint32_t owner, const char* name);
int32_t ss_system_open_writer(uint32_t owner, const char* name, int replace);
int32_t ss_system_read_fd(uint32_t owner, uint32_t fd, uint8_t* data, uint32_t size);
int32_t ss_system_write_fd(uint32_t owner, uint32_t fd, const uint8_t* data, uint32_t size);
int32_t ss_system_close_fd(uint32_t owner, uint32_t fd);
void ss_system_close_process_files(uint32_t owner);

#endif
