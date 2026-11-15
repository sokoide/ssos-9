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

/* Callers serialize mount and all file operations on a given device. The
 * physical block size must match the FAT logical sector size. Writable mount
 * rejects mismatched FAT copies and I/O errors without changing the volume.
 *
 * Write contract: every FAT entry update goes to all FAT copies on every
 * writable volume (Human68k included). Both mount paths (ss_storage_mount
 * and the Human68k ss_scsi_volume_attach) reject a volume whose FAT copies
 * already disagree, so allocation never starts from an ambiguous volume.
 * The update is per-copy read-modify-write, not atomic: a write failure in
 * the middle of a transaction may leave the copies inconsistent, and the
 * write_failed fence then blocks further writes until unmount and external
 * volume inspection. */
int ss_storage_mount(SSStorage* storage, SSFat12ReadSector read,
                     SSFatWriteSector write, void* context,
                     uint32_t sectors, uint16_t block_size);
void ss_storage_unmount(SSStorage* storage);
/* Returns 0 when every FAT copy of a mounted/probed volume is byte-identical
 * (a single-copy volume trivially matches); nonzero when copies disagree or
 * a read fails. Shared by both writable mount paths. */
int ss_storage_fat_copies_match(const SSFat12* volume);
/* replace=0 creates only, replace=1 replaces only. An I/O error or a failure
 * after a write was attempted fences subsequent writes until unmount and
 * external volume inspection. */
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
