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

static void exercise(uint32_t sector_size) {
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
    assert(memcmp(disk.bytes + sector_size, disk.bytes + mirror * sector_size,
                  fat_sectors * sector_size) == 0);
    disk.fail_write = (int32_t)mirror;
    disk.write_before_error = 1;
    assert(ss_storage_store(&storage, "FAIL.BIN", input, 1, 0) != 0);
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
    exercise(512);
    exercise(1024);
    return 0;
}
