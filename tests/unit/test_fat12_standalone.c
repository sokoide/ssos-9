#include "../../ssos/os/fs/fat12.h"
#include "../../ssos/os/fs/sector_adapter.h"
#include "../../ssos/os/fs/xdf.h"
#include "../../ssos/os/fs/scsi.h"
#include "../../ssos/os/fs/storage.h"
#include "../../ssos/os/fs/scsi_volume.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char disk[8][512];
static unsigned char disk1024[8][1024];
static uint32_t image_reads;
static int captured_mode, captured_position, captured_size;
static int32_t fake_iocs_status;
static int scsi_lba, scsi_target, scsi_code, scsi_status;
static uint8_t* scsi_disk;
static int fake_scsi_capacity(int target, uint8_t* data) {
    assert(target == 3);
    const uint8_t capacity[8] = {0, 0, 0x13, 0x91, 0, 0, 2, 0};
    memcpy(data, capacity, 8); /* last LBA 5009, 512-byte blocks */
    return scsi_status;
}
static int fake_scsi_read(int lba, int count, int target, int code, void* data) {
    assert(count == 1 && data);
    scsi_lba = lba; scsi_target = target; scsi_code = code;
    if (scsi_disk && !scsi_status) memcpy(data, scsi_disk + (uint32_t)lba * 512u, 512);
    return scsi_status;
}
static int fake_scsi_write(int lba, int count, int target, int code, const void* data) {
    assert(count == 1 && data);
    scsi_lba = lba; scsi_target = target; scsi_code = code;
    if (scsi_disk && !scsi_status) memcpy(scsi_disk + (uint32_t)lba * 512u, data, 512);
    return scsi_status;
}
typedef struct { uint8_t* bytes; int fail_sector; } WritableDisk;
static int memory_read(void* context, uint32_t n, uint8_t* out) {
    WritableDisk* disk = context;
    if (n >= 5000) return -1;
    memcpy(out, disk->bytes + n * 512u, 512);
    return 0;
}
static int memory_write(void* context, uint32_t n, const uint8_t* in) {
    WritableDisk* disk = context;
    if (n >= 5000 || (int)n == disk->fail_sector) return -1;
    memcpy(disk->bytes + n * 512u, in, 512);
    return 0;
}

static void test_fat16_create(void) {
    WritableDisk disk = {calloc(5000, 512), -1};
    assert(disk.bytes);
    uint8_t* boot = disk.bytes;
    boot[12] = 2; boot[13] = 1; boot[14] = 1; boot[16] = 2;
    boot[17] = 16; boot[19] = 0x88; boot[20] = 0x13; boot[22] = 20;
    boot[510] = 0x55; boot[511] = 0xaa;
    SSFat12 volume;
    SSFat12File file;
    uint8_t input[1400], actual[1400];
    for (unsigned i = 0; i < sizeof(input); i++) input[i] = (uint8_t)i;
    assert(ss_fat12_mount(&volume, memory_read, &disk, 5000) == 0);
    assert(ss_fat16_create(&volume, memory_write, "TEST.BIN", input, sizeof(input)) == 0);
    assert(memcmp(disk.bytes + 512, disk.bytes + 21 * 512, 20 * 512) == 0);
    assert(ss_fat12_open(&volume, "test.bin", &file) == 0);
    assert(ss_fat12_read(&file, actual, sizeof(actual)) == sizeof(actual));
    assert(memcmp(input, actual, sizeof(input)) == 0);
    assert(ss_fat16_create(&volume, memory_write, "test.bin", input, sizeof(input)) != 0);
    disk.fail_sector = 45;
    assert(ss_fat16_replace(&volume, memory_write, "test.bin", input, 1) != 0);
    assert(ss_fat12_open(&volume, "test.bin", &file) == 0);
    assert(ss_fat12_read(&file, actual, sizeof(actual)) == sizeof(actual));
    assert(memcmp(input, actual, sizeof(input)) == 0);
    disk.fail_sector = -1;
    assert(ss_fat16_replace(&volume, memory_write, "test.bin", input + 17, 600) == 0);
    assert(ss_fat12_open(&volume, "test.bin", &file) == 0 && file.size == 600);
    assert(ss_fat12_read(&file, actual, 600) == 600);
    assert(memcmp(input + 17, actual, 600) == 0);
    assert(disk.bytes[512 + 4] == 0 && disk.bytes[512 + 5] == 0);
    assert(ss_fat16_replace(&volume, memory_write, "test.bin", NULL, 0) == 0);
    assert(ss_fat12_open(&volume, "test.bin", &file) == 0 && file.size == 0);
    assert(ss_fat16_replace(&volume, memory_write, "MISSING", NULL, 0) != 0);
    assert(ss_fat16_create(&volume, memory_write, "EMPTY", NULL, 0) == 0);
    assert(ss_fat12_open(&volume, "EMPTY", &file) == 0 && file.size == 0);
    disk.fail_sector = 42; /* first free cluster's data sector */
    assert(ss_fat16_create(&volume, memory_write, "FAIL", input, 1) != 0);
    assert(ss_fat12_open(&volume, "FAIL", &file) != 0);
    assert(disk.bytes[512 + 4] == 0 && disk.bytes[512 + 5] == 0);
    disk.fail_sector = -1;
    memset(disk.bytes + 512, 0xff, 40 * 512);
    disk.bytes[512 + 4] = disk.bytes[512 + 5] = 0;
    disk.bytes[21 * 512 + 4] = disk.bytes[21 * 512 + 5] = 0;
    SSStorage storage = {0};
    assert(ss_storage_mount(&storage, memory_read, memory_write, &disk, 5000, 512) == 0);
    assert(ss_storage_store(&storage, "TOOBIG", input, 513, 0) != 0);
    assert(!storage.write_failed);
    assert(disk.bytes[512 + 4] == 0 && disk.bytes[512 + 5] == 0);
    assert(ss_storage_store(&storage, "FITS", input, 1, 0) == 0);
    assert(ss_storage_read(&storage, "FITS", actual, sizeof(actual)) == 1);
    assert(actual[0] == input[0]);
    free(disk.bytes);
}
/* Sparse 5000-sector FAT16 volume: cluster 2 -> 4096 -> EOC. */
static int read_fat16(void* context, uint32_t n, uint8_t* out) {
    int cycle = *(int*)context;
    if (n >= 5000) return -1;
    memset(out, 0, 512);
    if (n == 0) {
        out[12] = 2; out[13] = 1; out[14] = 1; out[16] = 1;
        out[17] = 16; out[19] = 0x88; out[20] = 0x13;
        out[22] = 20; out[510] = 0x55; out[511] = 0xaa;
    } else if (n == 1) {
        out[4] = 0; out[5] = 0x10;
    } else if (n == 17) {
        out[0] = cycle ? 2 : 0xff; out[1] = cycle ? 0 : 0xff;
    } else if (n == 21) {
        memcpy(out, "HELLO   S9M", 11);
        out[26] = 2; out[28] = 0x00; out[29] = 0x04;
    } else if (n == 22) {
        memset(out, 'A', 512);
    } else if (n == 4116) {
        memset(out, 'B', 512);
    }
    return 0;
}
static int fake_iocs(int mode, int position, int size, void* out) {
    captured_mode = mode; captured_position = position; captured_size = size;
    (void)out;
    return fake_iocs_status;
}
static int read_sector(void* context, uint32_t n, uint8_t* out) {
    (void)context;
    if (n >= 8) return -1;
    memcpy(out, disk[n], 512);
    return 0;
}

static int read_physical(void* context, uint32_t n, uint8_t* out) {
    (void)context;
    if (n >= 4) return -1;
    memcpy(out, disk[n * 2], 512);
    memcpy(out + 512, disk[n * 2 + 1], 512);
    return 0;
}

static int read_1024(void* context, uint32_t n, uint8_t* out) {
    (void)context;
    if (n >= 8) return -1;
    memcpy(out, disk1024[n], 1024);
    return 0;
}

static int read_image(void* context, uint32_t n, uint8_t* out) {
    FILE* file = context;
    image_reads++;
    return fseek(file, (long)n * 1024, SEEK_SET) ||
           fread(out, 1, 1024, file) != 1024 ? -1 : 0;
}

int main(int argc, char** argv) {
    test_fat16_create();
    uint8_t scsi_buffer[1024] __attribute__((aligned(4)));
    SSScsiDevice scsi = {fake_scsi_read, fake_scsi_write, 100000, 2048, 50000, 512, 3};
    assert(ss_scsi_read_sector(&scsi, 7, scsi_buffer) == 0);
    assert(scsi_lba == 2055 && scsi_target == 3 && scsi_code == 1);
    assert(ss_scsi_write_sector(&scsi, 49999, scsi_buffer) == 0);
    assert(scsi_lba == 52047);
    assert(ss_scsi_read_sector(&scsi, 50000, scsi_buffer) != 0);
    assert(ss_scsi_write_sector(&scsi, 0, scsi_buffer + 1) != 0);
    scsi_status = 2;
    assert(ss_scsi_write_sector(&scsi, 0, scsi_buffer) != 0);
    scsi_status = 0;
    scsi.volume_blocks = 100000;
    assert(ss_scsi_read_sector(&scsi, 0, scsi_buffer) != 0);
    scsi_disk = calloc(5010, 512);
    assert(scsi_disk);
    uint8_t* scsi_boot = scsi_disk + 10 * 512;
    scsi_boot[12] = 2; scsi_boot[13] = 1; scsi_boot[14] = 1; scsi_boot[16] = 2;
    scsi_boot[17] = 16; scsi_boot[19] = 0x88; scsi_boot[20] = 0x13;
    scsi_boot[22] = 20; scsi_boot[510] = 0x55; scsi_boot[511] = 0xaa;
    assert(ss_scsi_configure(&scsi, 3, 10, 0, fake_scsi_read, fake_scsi_write,
                             fake_scsi_capacity) == 0);
    assert(scsi.device_blocks == 5010 && scsi.volume_blocks == 5000);
    assert(ss_scsi_configure(&scsi, 3, 5009, 2, fake_scsi_read, fake_scsi_write,
                             fake_scsi_capacity) != 0);
    SSFat12 scsi_volume;
    SSFat12File scsi_file;
    uint8_t scsi_output[4];
    assert(ss_fat12_mount(&scsi_volume, ss_scsi_read_sector, &scsi, 5000) == 0);
    assert(ss_fat16_create(&scsi_volume, ss_scsi_write_sector, "DATA", (const uint8_t*)"test", 4) == 0);
    assert(ss_fat12_open(&scsi_volume, "DATA", &scsi_file) == 0);
    assert(ss_fat12_read(&scsi_file, scsi_output, 4) == 4);
    assert(memcmp(scsi_output, "test", 4) == 0);
    SSStorage storage = {0};
    assert(ss_storage_mount(&storage, ss_scsi_read_sector, ss_scsi_write_sector,
                            &scsi, 5000, 1024) != 0);
    assert(ss_storage_mount(&storage, ss_scsi_read_sector, ss_scsi_write_sector,
                            &scsi, 5000, 512) == 0);
    assert(ss_storage_store(&storage, "DATA", (const uint8_t*)"x", 1, 0) != 0);
    assert(ss_storage_store(&storage, "MISSING", NULL, 0, 1) != 0);
    assert(ss_storage_store(&storage, "BAD/NAME", NULL, 0, 0) != 0);
    assert(ss_storage_store(&storage, "DATA", (const uint8_t*)"new!", 4, 1) == 0);
    assert(ss_storage_read(&storage, "DATA", scsi_output, 4) == 4);
    assert(memcmp(scsi_output, "new!", 4) == 0);
    assert(ss_storage_read(&storage, "DATA", scsi_output, 3) != 0);
    scsi_status = 2;
    assert(ss_storage_store(&storage, "FAIL", (const uint8_t*)"x", 1, 0) != 0);
    scsi_status = 0;
    assert(ss_storage_store(&storage, "FAIL", (const uint8_t*)"x", 1, 0) != 0);
    /* The write fence blocks one-shot reads; fd readers still work. */
    assert(ss_storage_read(&storage, "DATA", scsi_output, 4) != 0);
    SSFat12File fenced;
    assert(ss_storage_open_reader(&storage, "DATA", &fenced) == 0);
    assert(ss_fat12_read(&fenced, scsi_output, 4) == 4);
    assert(memcmp(scsi_output, "new!", 4) == 0);
    ss_storage_close_reader(&storage, &fenced);
    ss_storage_unmount(&storage);
    assert(ss_storage_read(&storage, "DATA", scsi_output, 4) != 0);
    SSScsiVolume attached = {0};
    assert(ss_scsi_volume_attach(&attached, &scsi) != 0); /* MS-DOS volume rejected */
    free(scsi_disk);
    scsi_disk = NULL;
    int cycle = 0;
    SSFat12 fat16;
    SSFat12File fat16_file;
    uint8_t fat16_out[1024];
    assert(ss_fat12_mount(&fat16, read_fat16, &cycle, 5000) == 0);
    assert(fat16.fat_bits == 16);
    assert(ss_fat12_open(&fat16, "hello.s9m", &fat16_file) == 0);
    assert(ss_fat12_read(&fat16_file, fat16_out, sizeof(fat16_out)) == 1024);
    assert(fat16_out[0] == 'A' && fat16_out[511] == 'A');
    assert(fat16_out[512] == 'B' && fat16_out[1023] == 'B');
    cycle = 1;
    assert(ss_fat12_open(&fat16, "hello.s9m", &fat16_file) != 0);
    assert(ss_fat12_mount(&fat16, read_fat16, &cycle, 4999) != 0);
    disk[0][11] = 0; disk[0][12] = 2;
    disk[0][13] = 1; disk[0][14] = 1; disk[0][16] = 1;
    disk[0][17] = 16; disk[0][19] = 8; disk[0][22] = 1;
    disk[0][510] = 0x55; disk[0][511] = 0xaa;
    disk[1][3] = 0xff; disk[1][4] = 0x0f;
    memcpy(disk[2], "HELLO   S9M", 11);
    disk[2][26] = 2; disk[2][28] = 4;
    memcpy(disk[3], "test", 4);
    SSFat12 volume;
    SSFat12File file;
    uint8_t out[4];
    assert(ss_fat12_mount(&volume, read_sector, 0, 8) == 0);
    assert(ss_fat12_open(&volume, "hello.s9m", &file) == 0);
    assert(ss_fat12_read(&file, out, 2) == 2);
    assert(ss_fat12_read(&file, out + 2, 2) == 2);
    assert(memcmp(out, "test", 4) == 0);
    uint8_t aligned_sector[1024] __attribute__((aligned(4)));
    SSXdfReader xdf = {fake_iocs, 1};
    assert(ss_xdf_read_sector(&xdf, 0, aligned_sector) == 0);
    assert(captured_mode == 0x9170 && captured_position == 0x03000001 && captured_size == 1024);
    assert(ss_xdf_read_sector(&xdf, 1231, aligned_sector) == 0);
    assert(captured_position == 0x034c0108);
    assert(ss_xdf_read_sector(&xdf, 1232, aligned_sector) != 0);
    fake_iocs_status = 0x00100000;
    assert(ss_xdf_read_sector(&xdf, 0, aligned_sector) != 0);
    fake_iocs_status = 0x08000000;
    assert(ss_xdf_read_sector(&xdf, 0, aligned_sector) != 0);
    fake_iocs_status = 0x07000000;
    assert(ss_xdf_read_sector(&xdf, 0, aligned_sector) == 0);
    assert(ss_fat12_read(&file, out, 1) == 0);
    SSSectorAdapter adapter = {read_physical, 0, 4};
    assert(ss_fat12_mount(&volume, ss_sector_read512, &adapter, 8) == 0);
    assert(ss_fat12_open(&volume, "hello.s9m", &file) == 0);
    assert(ss_fat12_read(&file, out, 4) == 4);
    assert(memcmp(out, "test", 4) == 0);
    assert(ss_sector_read512(&adapter, 8, out) != 0);
    disk1024[0][12] = 4;
    disk1024[0][13] = 1; disk1024[0][14] = 1; disk1024[0][16] = 1;
    disk1024[0][17] = 32; disk1024[0][19] = 8; disk1024[0][22] = 1;
    disk1024[0][510] = 0x55; disk1024[0][511] = 0xaa;
    disk1024[1][3] = 0xff; disk1024[1][4] = 0x0f;
    /* Deleted entries before the last slot ensure 1024-byte directory sectors
     * are scanned in full rather than stopping at the 512-byte boundary. */
    for (unsigned i = 0; i < 31; i++) disk1024[2][i * 32] = 0xe5;
    memcpy(disk1024[2] + 31 * 32, "HELLO   S9M", 11);
    disk1024[2][31 * 32 + 26] = 2;
    disk1024[2][31 * 32 + 28] = 4;
    memcpy(disk1024[3], "test", 4);
    assert(ss_fat12_mount(&volume, read_1024, 0, 8) == 0);
    assert(ss_fat12_open(&volume, "hello.s9m", &file) == 0);
    assert(ss_fat12_read(&file, out, 4) == 4);
    assert(memcmp(out, "test", 4) == 0);
    disk[1][3] = 2; disk[1][4] = 0;
    disk[2][28] = 0; disk[2][29] = 4;
    assert(ss_fat12_mount(&volume, read_sector, 0, 8) == 0);
    assert(ss_fat12_open(&volume, "hello.s9m", &file) != 0);
    disk[0][510] = 0;
    assert(ss_fat12_mount(&volume, read_sector, 0, 8) != 0);
    if (argc == 3) {
        FILE* image = fopen(argv[1], "rb");
        FILE* source = fopen(argv[2], "rb");
        assert(image && source);
        assert(ss_fat12_mount(&volume, read_image, image, 1232) == 0);
        assert(ss_fat12_open(&volume, "HELLO.S9M", &file) == 0);
        uint32_t remaining = file.size;
        while (remaining) {
            uint8_t actual[256], expected[256];
            uint32_t count = remaining < sizeof(actual) ? remaining : sizeof(actual);
            assert(ss_fat12_read(&file, actual, count) == (int32_t)count);
            assert(fread(expected, 1, count, source) == count);
            assert(memcmp(actual, expected, count) == 0);
            remaining -= count;
        }
        assert(fgetc(source) == EOF);
        /* Reading in 256-byte chunks must not repeatedly traverse from the
         * beginning of a multi-cluster file. Includes open-time validation. */
        assert(image_reads < file.size / 256u + (file.size / 1024u + 1u) * 4u + 20u);
        fclose(source);
        fclose(image);
    }
    return 0;
}
