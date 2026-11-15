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

/* A writer updates every FAT copy, so never allocate from a volume whose
 * copies already disagree. Read-only FAT12 loading is deliberately separate. */
int ss_storage_fat_copies_match(const SSFat12* volume) {
    uint8_t primary[1024] __attribute__((aligned(4)));
    uint8_t mirror[1024] __attribute__((aligned(4)));
    if (volume->fat_count < 2) return 0;
    for (uint32_t sector = 0; sector < volume->fat_sectors; sector++) {
        if (volume->read_sector(volume->context, volume->fat_start + sector, primary))
            return -1;
        for (uint32_t copy = 1; copy < volume->fat_count; copy++) {
            uint32_t at = volume->fat_start + copy * volume->fat_sectors + sector;
            if (volume->read_sector(volume->context, at, mirror)) return -1;
            for (uint32_t i = 0; i < volume->sector_size; i++)
                if (primary[i] != mirror[i]) return -1;
        }
    }
    return 0;
}

int ss_storage_mount(SSStorage* storage, SSFat12ReadSector read,
                     SSFatWriteSector write, void* context,
                     uint32_t sectors, uint16_t block_size) {
    SSFat12 volume;
    if (!storage || storage->mounted || !write ||
        (block_size != 512 && block_size != 1024) ||
        ss_fat12_mount(&volume, read, context, sectors) ||
        volume.fat_bits != 16 || volume.sector_size != block_size ||
        ss_storage_fat_copies_match(&volume))
        return -1;
    storage->volume = volume;
    storage->write_sector = write;
    storage->readers = 0;
    storage->writer = 0;
    storage->write_failed = 0;
    storage->mounted = 1;
    return 0;
}

void ss_storage_unmount(SSStorage* storage) {
    if (!storage || storage->readers || storage->writer) return;
    storage->mounted = 0;
    storage->write_sector = NULL;
}

int ss_storage_store(SSStorage* storage, const char* name, const uint8_t* data,
                     uint32_t size, int replace) {
    if (!storage || !storage->mounted || storage->write_failed || storage->readers || storage->writer ||
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

int ss_storage_open_reader(SSStorage* storage, const char* name, SSFat12File* file) {
    if (!storage || !storage->mounted || storage->writer || !file || storage->readers == UINT8_MAX ||
        ss_fat12_open(&storage->volume, name, file)) return -1;
    storage->readers++;
    return 0;
}

int ss_storage_writer_begin(SSStorage* storage, const char* name, int replace,
                            SSFat16Writer* writer) {
    if (!storage || !storage->mounted || storage->write_failed || storage->readers ||
        storage->writer || !writer) return -1;
    SSStorageOperation op = {storage, 0, 0};
    SSFat12 volume = storage->volume;
    volume.context = &op;
    volume.read_sector = operation_read;
    int result = ss_fat16_writer_begin(&volume, name, replace, writer);
    if (op.io_failed) storage->write_failed = 1;
    if (!result) storage->writer = 1;
    return result;
}

int32_t ss_storage_writer_write(SSStorage* storage, SSFat16Writer* writer,
                                const uint8_t* data, uint32_t size) {
    if (!storage || !storage->writer || !writer || !writer->active) return -1;
    SSStorageOperation op = {storage, 0, 0};
    SSFat12 volume = storage->volume;
    volume.context = &op;
    volume.read_sector = operation_read;
    int32_t result = ss_fat16_writer_write(&volume, operation_write, writer, data, size);
    if (result < 0) {
        if (op.io_failed) {
            storage->write_failed = 1;
            writer->active = 0;
        } else if (ss_fat16_writer_abort(&volume, operation_write, writer)) {
            storage->write_failed = 1;
        }
        storage->writer = 0;
    }
    return result;
}

int ss_storage_writer_end(SSStorage* storage, SSFat16Writer* writer, int commit) {
    if (!storage || !storage->writer || !writer || !writer->active) return -1;
    SSStorageOperation op = {storage, 0, 0};
    SSFat12 volume = storage->volume;
    volume.context = &op;
    volume.read_sector = operation_read;
    int result = commit ? ss_fat16_writer_commit(&volume, operation_write, writer)
                        : ss_fat16_writer_abort(&volume, operation_write, writer);
    if (result) storage->write_failed = 1;
    storage->writer = 0;
    return result;
}

void ss_storage_close_reader(SSStorage* storage, SSFat12File* file) {
    if (!storage || !file || file->volume != &storage->volume || !storage->readers)
        return;
    storage->readers--;
    file->volume = NULL;
}

int32_t ss_storage_read(SSStorage* storage, const char* name, uint8_t* data,
                        uint32_t capacity) {
    if (!storage || !storage->mounted || storage->writer || storage->write_failed ||
        (!data && capacity)) return -1;
    SSFat12File file;
    if (ss_fat12_open(&storage->volume, name, &file) || file.size > capacity ||
        file.size > 0x7fffffffu) return -1;
    return ss_fat12_read(&file, data, file.size);
}
