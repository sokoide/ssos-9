#include "storage.h"

#include <stddef.h>

typedef struct {
    SSStorage* storage;
    int attempted_write;
    int io_failed;
} SSStorageOperation;

static int operation_read(void* context, uint32_t sector, uint8_t* out) {
    SSStorageOperation* op = context;
    int status = op->storage->volume.read_sector(op->storage->volume.context,
                                                  sector, out);
    if (status) op->io_failed = 1;
    return status;
}

static int operation_write(void* context, uint32_t sector, const uint8_t* in) {
    SSStorageOperation* op = context;
    op->attempted_write = 1;
    int status = op->storage->write_sector(op->storage->volume.context, sector, in);
    if (status) op->io_failed = 1;
    return status;
}

int ss_storage_mount(SSStorage* storage, SSFat12ReadSector read,
                     SSFatWriteSector write, void* context,
                     uint32_t sectors, uint16_t block_size) {
    SSFat12 volume;
    if (!storage || storage->mounted || !write ||
        (block_size != 512 && block_size != 1024) ||
        ss_fat12_mount(&volume, read, context, sectors) ||
        volume.fat_bits != 16 || volume.sector_size != block_size)
        return -1;
    storage->volume = volume;
    storage->write_sector = write;
    storage->write_failed = 0;
    storage->mounted = 1;
    return 0;
}

void ss_storage_unmount(SSStorage* storage) {
    if (!storage) return;
    storage->mounted = 0;
    storage->write_sector = NULL;
}

int ss_storage_store(SSStorage* storage, const char* name, const uint8_t* data,
                     uint32_t size, int replace) {
    if (!storage || !storage->mounted || storage->write_failed ||
        (replace != 0 && replace != 1)) return -1;
    SSStorageOperation operation = {storage, 0, 0};
    SSFat12 volume = storage->volume;
    volume.context = &operation;
    volume.read_sector = operation_read;
    int status = replace ? ss_fat16_replace(&volume, operation_write,
                                            name, data, size)
                         : ss_fat16_create(&volume, operation_write,
                                           name, data, size);
    if (status && (operation.attempted_write || operation.io_failed))
        storage->write_failed = 1;
    return status;
}

int32_t ss_storage_read(SSStorage* storage, const char* name, uint8_t* data,
                        uint32_t capacity) {
    if (!storage || !storage->mounted || (!data && capacity)) return -1;
    SSFat12File file;
    if (ss_fat12_open(&storage->volume, name, &file) || file.size > capacity ||
        file.size > 0x7fffffffu) return -1;
    return ss_fat12_read(&file, data, file.size);
}
