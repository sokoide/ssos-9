/* Storage-layer integrity on a Human68k-format volume reached through the
 * production attach path: reader positions, reader/writer exclusivity,
 * large files, a near-full volume, and the write fence after a failure in
 * each write phase. */
#include "../../ssos/os/fs/storage.h"
#include "human68k_disk.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STORE_SECTORS 320u

static SSStorage* storage;

/* The raw SSFatWriteSector the FAT writer sees, without the storage
 * layer's failure tracking. */
static int raw_write(void* context, uint32_t sector, const uint8_t* in) {
    (void)context; /* the volume passes its device, not the storage */
    return storage->write_sector(storage->volume.context, sector, in);
}

/* 512-byte block of a sector counted from the start of the disk (the HD_
 * offsets). Volume-relative sectors (SSFat12 and SSFat16Writer) start at
 * the partition, so add HD_PARTITION_START first. */
static uint32_t block_of(uint32_t disk_sector) {
    return disk_sector * (HD_SECTOR / HD_BLOCK_BYTES);
}

/* Marks every cluster but one as allocated. Returns the cluster left free. */
static uint32_t fill_volume(uint8_t* saved_fat) {
    memcpy(saved_fat, hd_fat(), HD_FAT_SECTORS * HD_SECTOR);
    uint32_t free_cluster = 0;
    for (uint32_t cluster = 2; cluster < HD_CLUSTERS + 2u; cluster++) {
        uint8_t* entry = hd_fat_entry(cluster);
        if (entry[0] || entry[1]) continue;
        if (!free_cluster) {
            free_cluster = cluster;
            continue;
        }
        entry[0] = entry[1] = 0xff;
    }
    assert(free_cluster);
    return free_cluster;
}

static void exercise(int stream_failure) {
    static uint8_t store[HD_STORE_BYTES(STORE_SECTORS)];
    uint8_t* saved_fat = malloc(HD_FAT_SECTORS * HD_SECTOR);
    assert(saved_fat);
    hd_format(store, STORE_SECTORS, 0);
    SSScsiVolume volume = {0};
    assert(hd_mount(&volume, 3, 0, 0) == 0);
    storage = &volume.storage;
    uint8_t input[2500], actual[2500];
    for (unsigned i = 0; i < sizeof(input); i++) input[i] = (uint8_t)i;
    assert(ss_storage_store(storage, "GOOD.BIN", input, sizeof(input), 0) == 0);
    assert(ss_storage_read(storage, "GOOD.BIN", actual, sizeof(actual)) == sizeof(actual));
    assert(memcmp(input, actual, sizeof(input)) == 0);
    SSFat12File first, second;
    assert(ss_storage_open_reader(storage, "GOOD.BIN", &first) == 0);
    assert(ss_storage_open_reader(storage, "GOOD.BIN", &second) == 0);
    /* The first data sector of GOOD.BIN (cluster 3; cluster 2 is TEST). */
    hd_disk.fail_block = (int32_t)block_of(HD_DATA_START + 1u);
    hd_disk.fail_reads = 1;
    assert(ss_fat12_read(&first, actual, 1500) == -1);
    assert(first.offset == 0);
    hd_disk.fail_block = -1;
    hd_disk.fail_reads = 0;
    assert(ss_fat12_read(&first, actual, 777) == 777);
    assert(ss_fat12_read(&second, actual + 777, 13) == 13);
    assert(memcmp(actual, input, 777) == 0);
    assert(memcmp(actual + 777, input, 13) == 0);
    assert(ss_storage_store(storage, "BUSY.BIN", input, 1, 0) != 0);
    ss_storage_close_reader(storage, &second);
    assert(ss_storage_store(storage, "BUSY.BIN", input, 1, 0) != 0);
    assert(ss_fat12_read(&first, actual + 777, sizeof(actual) - 777) ==
           sizeof(actual) - 777);
    assert(memcmp(actual, input, sizeof(input)) == 0);
    assert(ss_fat12_read(&first, actual, 1) == 0);
    ss_storage_close_reader(storage, &first);
    ss_storage_close_reader(storage, &first);
    assert(ss_storage_store(storage, "BUSY.BIN", input, 1, 0) == 0);
    uint32_t big_size = 70000;
    uint8_t* big = malloc(big_size);
    uint8_t* copy = malloc(big_size);
    assert(big && copy);
    for (uint32_t i = 0; i < big_size; i++) big[i] = (uint8_t)(i * 17u);
    assert(ss_storage_store(storage, "BIG.BIN", big, big_size, 0) == 0);
    assert(ss_storage_open_reader(storage, "BIG.BIN", &first) == 0);
    assert(ss_fat12_read(&first, copy, 65536) == 65536);
    assert(ss_fat12_read(&first, copy + 65536, big_size - 65536) ==
           (int32_t)(big_size - 65536));
    assert(memcmp(big, copy, big_size) == 0);
    ss_storage_close_reader(storage, &first);
    SSFat16Writer large_writer;
    assert(ss_storage_writer_begin(storage, "LARGE.BIN", 0, &large_writer) == 0);
    /* A one-shot read must not observe a transaction in flight. */
    assert(ss_storage_read(storage, "GOOD.BIN", actual, sizeof(actual)) == -1);
    assert(ss_storage_writer_write(storage, &large_writer, big, 65536) == 65536);
    assert(ss_storage_writer_write(storage, &large_writer, big + 65536,
                                   big_size - 65536) == (int32_t)(big_size - 65536));
    assert(ss_storage_writer_end(storage, &large_writer, 1) == 0);
    assert(ss_storage_open_reader(storage, "LARGE.BIN", &first) == 0);
    assert(ss_fat12_read(&first, copy, big_size) == (int32_t)big_size);
    assert(memcmp(big, copy, big_size) == 0);
    ss_storage_close_reader(storage, &first);
    free(copy);
    free(big);
    SSFat16Writer writer;
    assert(ss_fat16_writer_begin(&storage->volume, "STREAM.BIN", 0, &writer) == 0);
    assert(ss_fat16_writer_write(&storage->volume, raw_write, &writer, input, 37) == 37);
    assert(ss_fat16_writer_write(&storage->volume, raw_write, &writer,
                                 input + 37, sizeof(input) - 37) == sizeof(input) - 37);
    assert(ss_storage_read(storage, "STREAM.BIN", actual, sizeof(actual)) == -1);
    assert(ss_fat16_writer_commit(&storage->volume, raw_write, &writer) == 0);
    assert(ss_storage_read(storage, "STREAM.BIN", actual, sizeof(actual)) == sizeof(actual));
    assert(memcmp(input, actual, sizeof(input)) == 0);
    assert(ss_fat16_writer_begin(&storage->volume, "STREAM.BIN", 1, &writer) == 0);
    assert(ss_fat16_writer_write(&storage->volume, raw_write, &writer, input, 100) == 100);
    assert(ss_fat16_writer_abort(&storage->volume, raw_write, &writer) == 0);
    assert(ss_storage_read(storage, "STREAM.BIN", actual, sizeof(actual)) == sizeof(actual));
    assert(memcmp(input, actual, sizeof(input)) == 0);
    assert(ss_fat16_writer_begin(&storage->volume, "STREAM.BIN", 1, &writer) == 0);
    assert(ss_fat16_writer_write(&storage->volume, raw_write, &writer, input, 100) == 100);
    assert(ss_fat16_writer_commit(&storage->volume, raw_write, &writer) == 0);
    assert(ss_storage_read(storage, "STREAM.BIN", actual, sizeof(actual)) == 100);
    assert(memcmp(input, actual, 100) == 0);
    /* Human68k reserves FAT2; every successful write must leave it zero. */
    for (uint32_t i = 0; i < HD_FAT_SECTORS * HD_SECTOR; i++)
        assert(hd_logical(HD_FAT2_START)[i] == 0);
    uint32_t free_cluster = fill_volume(saved_fat);
    assert(ss_storage_writer_begin(storage, "STREAM.BIN", 1, &writer) == 0);
    hd_disk.reads = 0;
    assert(ss_storage_writer_write(storage, &writer, input, HD_SECTOR * 2u) == -1);
    /* A near-full volume is scanned by FAT sector, including the wrap. */
    assert(hd_disk.reads <= HD_FAT_SECTORS * 2u + 16u);
    assert(!storage->write_failed && !storage->writer);
    assert(ss_storage_read(storage, "STREAM.BIN", actual, sizeof(actual)) == 100);
    assert(memcmp(input, actual, 100) == 0);
    memcpy(hd_fat(), saved_fat, HD_FAT_SECTORS * HD_SECTOR);
    free(saved_fat);
    hd_disk.write_before_error = 1;
    if (stream_failure == 2) {
        /* The directory entry's own write reports an error. */
        assert(ss_storage_writer_begin(storage, "FAIL.BIN", 0, &writer) == 0);
        assert(ss_storage_writer_write(storage, &writer, input, 1) == 1);
        hd_disk.fail_block =
            (int32_t)block_of(HD_PARTITION_START + writer.human_sector);
        assert(ss_storage_writer_end(storage, &writer, 1) != 0);
        assert(!storage->writer && !writer.active);
    } else if (stream_failure == 1) {
        /* The new chain's first data sector fails inside the stream. */
        assert(ss_storage_writer_begin(storage, "FAIL.BIN", 0, &writer) == 0);
        hd_disk.fail_block = (int32_t)block_of(HD_DATA_START + free_cluster - 2u);
        assert(ss_storage_writer_write(storage, &writer, input, 1) < 0);
        assert(!storage->writer && !writer.active);
    } else if (stream_failure == 3) {
        /* The FAT entry that allocates the chain fails to reach the disk. */
        hd_disk.fail_block =
            (int32_t)block_of(HD_FAT_START + free_cluster * 2u / HD_SECTOR);
        assert(ss_storage_store(storage, "FAIL.BIN", input, 1, 0) != 0);
    } else {
        hd_disk.fail_block = (int32_t)block_of(HD_DATA_START + free_cluster - 2u);
        assert(ss_storage_store(storage, "FAIL.BIN", input, 1, 0) != 0);
    }
    assert(storage->write_failed);
    unsigned after_failure = hd_disk.writes;
    hd_disk.fail_block = -1;
    assert(ss_storage_store(storage, "LATER.BIN", input, 1, 0) != 0);
    assert(hd_disk.writes == after_failure);
    /* The write fence also blocks one-shot reads; fd-based readers stay
     * available for inspecting the fenced volume. */
    assert(ss_storage_read(storage, "GOOD.BIN", actual, sizeof(actual)) == -1);
    SSFat12File fenced;
    assert(ss_storage_open_reader(storage, "GOOD.BIN", &fenced) == 0);
    assert(ss_fat12_read(&fenced, actual, sizeof(actual)) == sizeof(actual));
    assert(memcmp(input, actual, sizeof(input)) == 0);
    ss_storage_close_reader(storage, &fenced);
}

int main(void) {
    for (int mode = 0; mode < 4; mode++) exercise(mode);
    puts("storage integrity tests passed");
    return 0;
}
