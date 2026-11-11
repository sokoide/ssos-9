#include "../../ssos/os/fs/storage.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SECTORS 5000u

typedef struct {
    uint8_t* bytes;
    uint32_t sector_size;
    int32_t fail_read;
    int32_t fail_write;
    unsigned writes;
    int write_before_error;
} TestDisk;

static int read_sector(void* context, uint32_t sector, uint8_t* out) {
    TestDisk* disk = context;
    if (sector >= SECTORS || (int32_t)sector == disk->fail_read) return -1;
    memcpy(out, disk->bytes + sector * disk->sector_size, disk->sector_size);
    return 0;
}

static int write_sector(void* context, uint32_t sector, const uint8_t* in) {
    TestDisk* disk = context;
    disk->writes++;
    if (sector >= SECTORS) return -1;
    if ((int32_t)sector == disk->fail_write) {
        if (disk->write_before_error)
            memcpy(disk->bytes + sector * disk->sector_size, in, disk->sector_size);
        return -1;
    }
    memcpy(disk->bytes + sector * disk->sector_size, in, disk->sector_size);
    return 0;
}

static void exercise(uint32_t sector_size, int stream_failure) {
    TestDisk disk = {calloc(SECTORS, sector_size), sector_size, -1, -1, 0, 0};
    assert(disk.bytes);
    uint8_t* boot = disk.bytes;
    uint16_t fat_sectors = sector_size == 512 ? 20 : 10;
    boot[11] = (uint8_t)sector_size;
    boot[12] = (uint8_t)(sector_size >> 8);
    boot[13] = 1;
    boot[14] = 1;
    boot[16] = 2;
    boot[17] = 32;
    boot[19] = (uint8_t)SECTORS;
    boot[20] = (uint8_t)(SECTORS >> 8);
    boot[22] = (uint8_t)fat_sectors;
    boot[510] = 0x55;
    boot[511] = 0xaa;
    SSStorage storage = {0};
    uint32_t mirror = 1u + fat_sectors;
    assert(ss_storage_mount(&storage, read_sector, write_sector, &disk,
                            SECTORS - 1u, (uint16_t)sector_size) != 0);
    assert(!storage.mounted && disk.writes == 0);
    disk.bytes[mirror * sector_size + 4] = 1;
    assert(ss_storage_mount(&storage, read_sector, write_sector, &disk,
                            SECTORS, (uint16_t)sector_size) != 0);
    assert(!storage.mounted && disk.writes == 0);
    disk.bytes[mirror * sector_size + 4] = 0;
    disk.fail_read = (int32_t)mirror;
    assert(ss_storage_mount(&storage, read_sector, write_sector, &disk,
                            SECTORS, (uint16_t)sector_size) != 0);
    assert(!storage.mounted && disk.writes == 0);
    disk.fail_read = -1;
    assert(ss_storage_mount(&storage, read_sector, write_sector, &disk,
                            SECTORS, (uint16_t)sector_size) == 0);
    uint8_t input[2500], actual[2500];
    for (unsigned i = 0; i < sizeof(input); i++) input[i] = (uint8_t)i;
    assert(ss_storage_store(&storage, "GOOD.BIN", input, sizeof(input), 0) == 0);
    assert(ss_storage_read(&storage, "GOOD.BIN", actual, sizeof(actual)) == sizeof(actual));
    assert(memcmp(input, actual, sizeof(input)) == 0);
    SSFat12File first, second;
    assert(ss_storage_open_reader(&storage, "GOOD.BIN", &first) == 0);
    assert(ss_storage_open_reader(&storage, "GOOD.BIN", &second) == 0);
    disk.fail_read = (int32_t)storage.volume.data_start + 1;
    assert(ss_fat12_read(&first, actual, 1500) == -1);
    assert(first.offset == 0);
    disk.fail_read = -1;
    assert(ss_fat12_read(&first, actual, 777) == 777);
    assert(ss_fat12_read(&second, actual + 777, 13) == 13);
    assert(memcmp(actual, input, 777) == 0);
    assert(memcmp(actual + 777, input, 13) == 0);
    assert(ss_storage_store(&storage, "BUSY.BIN", input, 1, 0) != 0);
    ss_storage_unmount(&storage);
    assert(storage.mounted);
    ss_storage_close_reader(&storage, &second);
    assert(ss_storage_store(&storage, "BUSY.BIN", input, 1, 0) != 0);
    assert(ss_fat12_read(&first, actual + 777, sizeof(actual) - 777) ==
           sizeof(actual) - 777);
    assert(memcmp(actual, input, sizeof(input)) == 0);
    assert(ss_fat12_read(&first, actual, 1) == 0);
    ss_storage_close_reader(&storage, &first);
    ss_storage_close_reader(&storage, &first);
    assert(ss_storage_store(&storage, "BUSY.BIN", input, 1, 0) == 0);
    uint32_t big_size = 70000;
    uint8_t* big = malloc(big_size);
    uint8_t* copy = malloc(big_size);
    assert(big && copy);
    for (uint32_t i = 0; i < big_size; i++) big[i] = (uint8_t)(i * 17u);
    assert(ss_storage_store(&storage, "BIG.BIN", big, big_size, 0) == 0);
    assert(ss_storage_open_reader(&storage, "BIG.BIN", &first) == 0);
    assert(ss_fat12_read(&first, copy, 65536) == 65536);
    assert(ss_fat12_read(&first, copy + 65536, big_size - 65536) ==
           (int32_t)(big_size - 65536));
    assert(memcmp(big, copy, big_size) == 0);
    ss_storage_close_reader(&storage, &first);
    SSFat16Writer large_writer;
    assert(ss_storage_writer_begin(&storage, "LARGE.BIN", 0, &large_writer) == 0);
    assert(ss_storage_writer_write(&storage, &large_writer, big, 65536) == 65536);
    assert(ss_storage_writer_write(&storage, &large_writer, big + 65536,
                                   big_size - 65536) == (int32_t)(big_size - 65536));
    assert(ss_storage_writer_end(&storage, &large_writer, 1) == 0);
    assert(ss_storage_open_reader(&storage, "LARGE.BIN", &first) == 0);
    assert(ss_fat12_read(&first, copy, big_size) == (int32_t)big_size);
    assert(memcmp(big, copy, big_size) == 0);
    ss_storage_close_reader(&storage, &first);
    free(copy);
    free(big);
    SSFat16Writer writer;
    assert(ss_fat16_writer_begin(&storage.volume, "STREAM.BIN", 0, &writer) == 0);
    assert(ss_fat16_writer_write(&storage.volume, write_sector, &writer, input, 37) == 37);
    assert(ss_fat16_writer_write(&storage.volume, write_sector, &writer,
                                 input + 37, sizeof(input) - 37) == sizeof(input) - 37);
    assert(ss_storage_read(&storage, "STREAM.BIN", actual, sizeof(actual)) == -1);
    assert(ss_fat16_writer_commit(&storage.volume, write_sector, &writer) == 0);
    assert(ss_storage_read(&storage, "STREAM.BIN", actual, sizeof(actual)) == sizeof(actual));
    assert(memcmp(input, actual, sizeof(input)) == 0);
    assert(ss_fat16_writer_begin(&storage.volume, "STREAM.BIN", 1, &writer) == 0);
    assert(ss_fat16_writer_write(&storage.volume, write_sector, &writer, input, 100) == 100);
    assert(ss_fat16_writer_abort(&storage.volume, write_sector, &writer) == 0);
    assert(ss_storage_read(&storage, "STREAM.BIN", actual, sizeof(actual)) == sizeof(actual));
    assert(memcmp(input, actual, sizeof(input)) == 0);
    assert(ss_fat16_writer_begin(&storage.volume, "STREAM.BIN", 1, &writer) == 0);
    assert(ss_fat16_writer_write(&storage.volume, write_sector, &writer, input, 100) == 100);
    assert(ss_fat16_writer_commit(&storage.volume, write_sector, &writer) == 0);
    assert(ss_storage_read(&storage, "STREAM.BIN", actual, sizeof(actual)) == 100);
    assert(memcmp(input, actual, 100) == 0);
    size_t fat_bytes = fat_sectors * sector_size;
    uint8_t* saved_fat = malloc(fat_bytes);
    assert(saved_fat);
    memcpy(saved_fat, disk.bytes + sector_size, fat_bytes);
    unsigned free_left = 1;
    for (uint32_t cluster = 2; cluster < storage.volume.cluster_count + 2u; cluster++) {
        uint32_t pos = cluster * 2u;
        uint8_t* entry = disk.bytes + sector_size + pos;
        if (!entry[0] && !entry[1]) {
            if (free_left) { free_left--; continue; }
            entry[0] = entry[1] = 0xff;
        }
    }
    memcpy(disk.bytes + mirror * sector_size, disk.bytes + sector_size, fat_bytes);
    assert(ss_storage_writer_begin(&storage, "STREAM.BIN", 1, &writer) == 0);
    assert(ss_storage_writer_write(&storage, &writer, input, sector_size * 2u) == -1);
    assert(!storage.write_failed && !storage.writer);
    assert(ss_storage_read(&storage, "STREAM.BIN", actual, sizeof(actual)) == 100);
    assert(memcmp(input, actual, 100) == 0);
    memcpy(disk.bytes + sector_size, saved_fat, fat_bytes);
    memcpy(disk.bytes + mirror * sector_size, saved_fat, fat_bytes);
    free(saved_fat);
    assert(memcmp(disk.bytes + sector_size, disk.bytes + mirror * sector_size,
                  fat_sectors * sector_size) == 0);
    uint32_t free_cluster = 2;
    const uint8_t* fat = disk.bytes + sector_size;
    while (free_cluster < storage.volume.cluster_count + 2u &&
           (fat[free_cluster * 2u] || fat[free_cluster * 2u + 1u])) free_cluster++;
    assert(free_cluster < storage.volume.cluster_count + 2u);
    disk.write_before_error = 1;
    if (stream_failure == 2) {
        assert(ss_storage_writer_begin(&storage, "FAIL.BIN", 0, &writer) == 0);
        assert(ss_storage_writer_write(&storage, &writer, input, 1) == 1);
        disk.fail_write = (int32_t)(storage.volume.root_start +
                                     writer.slot / (sector_size / 32u));
        assert(ss_storage_writer_end(&storage, &writer, 1) != 0);
        assert(!storage.writer && !writer.active);
    } else if (stream_failure) {
        assert(ss_storage_writer_begin(&storage, "FAIL.BIN", 0, &writer) == 0);
        disk.fail_write = (int32_t)(mirror + free_cluster * 2u / sector_size);
        assert(ss_storage_writer_write(&storage, &writer, input, 1) < 0);
        assert(!storage.writer && !writer.active);
    } else {
        disk.fail_write = (int32_t)(mirror + free_cluster * 2u / sector_size);
        assert(ss_storage_store(&storage, "FAIL.BIN", input, 1, 0) != 0);
    }
    assert(storage.write_failed);
    unsigned after_failure = disk.writes;
    disk.fail_write = -1;
    assert(ss_storage_store(&storage, "LATER.BIN", input, 1, 0) != 0);
    assert(disk.writes == after_failure);
    assert(ss_storage_read(&storage, "GOOD.BIN", actual, sizeof(actual)) == sizeof(actual));
    assert(memcmp(input, actual, sizeof(input)) == 0);
    free(disk.bytes);
}

int main(void) {
    exercise(512, 0);
    exercise(1024, 0);
    exercise(512, 1);
    exercise(1024, 1);
    exercise(512, 2);
    exercise(1024, 2);
    return 0;
}
