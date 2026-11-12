#include "../../ssos/os/fs/scsi_volume.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t* disk;
static int block_bytes = 512;
static int fail_lba = -1;
static int rd(int lba, int count, int target, int code, void* out) {
    if (lba < 0 || lba >= 10240000 / block_bytes || count != 1 || target != 0 || code != (block_bytes == 512 ? 1 : 2)) return -1;
    memcpy(out, disk + (uint32_t)lba * (uint32_t)block_bytes, (size_t)block_bytes); return 0;
}
static int wr(int lba, int count, int target, int code, const void* in) {
    if (lba < 0 || lba >= 10240000 / block_bytes || count != 1 || target != 0 || code != (block_bytes == 512 ? 1 : 2) || lba == fail_lba) return -1;
    memcpy(disk + (uint32_t)lba * (uint32_t)block_bytes, in, (size_t)block_bytes); return 0;
}
static void b16(uint8_t* p, uint16_t n) { p[0] = n >> 8; p[1] = n; }
static void b32(uint8_t* p, uint32_t n) { p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n; }
int main(void) {
    disk = calloc(10000, 1024); assert(disk);
    memcpy(disk, "X68SCSI1", 8);
    uint8_t* table = disk + 2u * 1024u;
    memcpy(table, "X68K", 4); memcpy(table + 16, "Human68k", 8);
    b32(table + 24, 32); b32(table + 28, 9000);
    uint8_t* ipl = disk + 32u * 1024u;
    b16(ipl + 0x12, 1024); ipl[0x14] = 1; ipl[0x15] = 2;
    b16(ipl + 0x16, 1); b16(ipl + 0x18, 512);
    ipl[0x1c] = 0xf7; ipl[0x1d] = 18; b32(ipl + 0x1e, 9000); b32(ipl + 0x22, 32);
    uint8_t* fat = disk + (32u + 1u) * 1024u;
    fat[0] = 0xf7; fat[1] = fat[2] = fat[3] = 0xff;
    fat[4] = fat[5] = 0xff; /* cluster 2: TEST directory */
    uint8_t* root = disk + (32u + 37u) * 1024u;
    memcpy(root, "TEST       ", 11); root[11] = 0x10; root[26] = 2;
    uint8_t* dir = disk + (32u + 53u) * 1024u;
    memcpy(dir, ".          ", 11); dir[11] = 0x10; dir[26] = 2;
    memcpy(dir + 32, "..         ", 11); dir[43] = 0x10;
    SSScsiDevice device = {rd, wr, 20000, 0, 20000, 512, 0};
    SSScsiVolume volume = {0};
    assert(ss_scsi_volume_attach(&volume, &device) == 0);
    const uint8_t msg[] = "hello"; uint8_t out[8] = {0};
    assert(ss_storage_store(&volume.storage, "A:\\TEST\\NEW.TXT", msg, 5, 0) == 0);
    assert(fat[6] == 0xff && fat[7] == 0xff);
    assert(!memcmp(dir + 64, "NEW     TXT", 11));
    assert(dir[64 + 26] == 3 && dir[64 + 27] == 0);
    assert(dir[64 + 28] == 5 && dir[64 + 29] == 0);
    assert(ss_storage_read(&volume.storage, "test/new.txt", out, 8) == 5);
    assert(!memcmp(out, msg, 5));
    assert(ss_storage_store(&volume.storage, "A:\\TEST\\NEW.TXT", (const uint8_t*)"world", 5, 1) == 0);
    assert(fat[6] == 0 && fat[7] == 0 && fat[8] == 0xff && fat[9] == 0xff);
    assert(dir[64 + 26] == 4 && dir[64 + 27] == 0);
    assert(ss_storage_read(&volume.storage, "TEST/NEW.TXT", out, 8) == 5);
    assert(!memcmp(out, "world", 5));
    assert(ss_storage_store(&volume.storage, "A:\\TEST\\..\\BAD", msg, 5, 0) != 0);
    assert(ss_storage_store(&volume.storage, "A:\\TEST\\NEW.TXT", msg, 5, 0) != 0);
    assert(ss_storage_store(&volume.storage, "TEST/EMPTY", NULL, 0, 0) == 0);
    assert(ss_storage_read(&volume.storage, "TEST/EMPTY", out, 8) == 0);
    SSFat16Writer writer;
    assert(ss_storage_writer_begin(&volume.storage, "TEST/STREAM", 0, &writer) == 0);
    assert(ss_storage_writer_write(&volume.storage, &writer, msg, 2) == 2);
    assert(ss_storage_writer_write(&volume.storage, &writer, msg + 2, 3) == 3);
    assert(ss_storage_writer_end(&volume.storage, &writer, 1) == 0);
    assert(ss_storage_read(&volume.storage, "TEST/STREAM", out, 8) == 5 && !memcmp(out, msg, 5));
    assert(!memcmp(root, "TEST       ", 11));
    assert(fat[4] == 0xff && fat[5] == 0xff);
    uint8_t saved_fat[1024];
    memcpy(saved_fat, fat, sizeof(saved_fat));
    fail_lba = 33 * 2 + 1; /* second physical block of a logical FAT write */
    assert(ss_storage_store(&volume.storage, "TEST/FAIL512", msg, 5, 0) != 0);
    assert(volume.storage.write_failed);
    fail_lba = -1;
    assert(ss_storage_store(&volume.storage, "TEST/BLOCKED", msg, 5, 0) != 0);
    memcpy(fat, saved_fat, sizeof(saved_fat)); /* fixture recovery */
    memset(&volume, 0, sizeof(volume));
    block_bytes = 1024;
    device.device_blocks = device.volume_blocks = 10000; device.block_size = 1024;
    assert(ss_scsi_volume_attach(&volume, &device) == 0);
    assert(ss_storage_read(&volume.storage, "TEST/STREAM", out, 8) == 5 && !memcmp(out, msg, 5));
    fail_lba = 33;
    assert(ss_storage_store(&volume.storage, "TEST/FAIL", msg, 5, 0) != 0);
    assert(volume.storage.write_failed);
    fail_lba = -1;
    assert(ss_storage_store(&volume.storage, "TEST/BLOCKED", msg, 5, 0) != 0);
    for (unsigned i = 4; i < 18u * 1024u; i += 2) {
        uint8_t t = fat[i]; fat[i] = fat[i + 1]; fat[i + 1] = t;
    }
    ipl[0x15] |= 0x80;
    memset(&volume, 0, sizeof(volume));
    assert(ss_scsi_volume_attach(&volume, &device) == 0);
    assert(ss_storage_read(&volume.storage, "TEST/STREAM", out, 8) == 5 && !memcmp(out, msg, 5));
    assert(ss_storage_store(&volume.storage, "TEST/INTEL", msg, 5, 0) == 0);
    assert(ss_storage_read(&volume.storage, "TEST/INTEL", out, 8) == 5 && !memcmp(out, msg, 5));
    memset(dir, ' ', 1024);
    for (unsigned i = 0; i < 31; i++) dir[i * 32u] = 'Q';
    dir[31u * 32u] = 0;
    fat[4] = 50; fat[5] = 0; fat[100] = fat[101] = 0xff;
    uint8_t* next_dir = disk + (32u + 53u + 48u) * 1024u;
    next_dir[0] = 'X';
    assert(ss_storage_store(&volume.storage, "TEST/BOUND.TXT", msg, 5, 0) == 0);
    assert(!memcmp(dir + 31u * 32u, "BOUND   TXT", 11));
    assert(next_dir[0] == 0);
    assert(ss_storage_read(&volume.storage, "TEST/BOUND.TXT", out, 8) == 5 && !memcmp(out, msg, 5));
    free(disk);
    puts("Human68k storage tests passed");
    return 0;
}
