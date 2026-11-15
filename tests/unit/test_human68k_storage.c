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
    uint8_t* fat2 = disk + (32u + 1u + 18u) * 1024u; /* second FAT copy */
    fat[0] = 0xf7; fat[1] = fat[2] = fat[3] = 0xff;
    fat[4] = fat[5] = 0xff; /* cluster 2: TEST directory */
    memcpy(fat2, fat, 1024u); /* both copies start identical */
    uint8_t* root = disk + (32u + 37u) * 1024u;
    memcpy(root, "TEST       ", 11); root[11] = 0x10; root[26] = 2;
    uint8_t* dir = disk + (32u + 53u) * 1024u;
    memcpy(dir, ".          ", 11); dir[11] = 0x10; dir[26] = 2;
    memcpy(dir + 32, "..         ", 11); dir[43] = 0x10;
    SSScsiDevice device = {rd, wr, 20000, 0, 20000, 512, 0};
    SSScsiVolume volume = {0};
    fat2[5] = 0x12; /* F-03: copies disagree -> write-attach must reject */
    assert(ss_scsi_volume_attach(&volume, &device) != 0);
    assert(!volume.storage.mounted);
    fat2[5] = 0xff;
    assert(ss_scsi_volume_attach(&volume, &device) == 0);
    const uint8_t msg[] = "hello"; uint8_t out[8] = {0};
    assert(ss_storage_store(&volume.storage, "A:\\TEST\\NEW.TXT", msg, 5, 0) == 0);
    assert(fat[6] == 0xff && fat[7] == 0xff);
    assert(!memcmp(fat, fat2, 18u * 1024u)); /* every FAT copy updated */
    assert(!memcmp(dir + 64, "NEW     TXT", 11));
    assert(dir[64 + 26] == 3 && dir[64 + 27] == 0);
    assert(dir[64 + 28] == 5 && dir[64 + 29] == 0);
    /* SSOS-written entries keep reserved bytes 0x0C-0x15 zero so they
     * round-trip through the Human68k lookup rules. */
    for (unsigned k = 12; k < 22; k++) assert(!dir[64 + k]);
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
    /* R-07: a zero-length commit stores first=0 and consumes no cluster.
     * In use: 2 = TEST dir, 4 = NEW.TXT; 3 and 5 stay free. */
    assert(!dir[96 + 26] && !dir[96 + 27]);
    assert(!fat[10] && !fat[11]);
    SSFat12File empty_fd;
    assert(ss_storage_open_reader(&volume.storage, "TEST/EMPTY", &empty_fd) == 0);
    assert(empty_fd.size == 0 && ss_fat12_read(&empty_fd, out, 8) == 0);
    ss_storage_close_reader(&volume.storage, &empty_fd);
    SSFat16Writer writer;
    assert(ss_storage_writer_begin(&volume.storage, "TEST/STREAM", 0, &writer) == 0);
    assert(ss_storage_writer_write(&volume.storage, &writer, msg, 2) == 2);
    assert(ss_storage_writer_write(&volume.storage, &writer, msg + 2, 3) == 3);
    assert(ss_storage_writer_end(&volume.storage, &writer, 1) == 0);
    assert(ss_storage_read(&volume.storage, "TEST/STREAM", out, 8) == 5 && !memcmp(out, msg, 5));
    assert(!memcmp(fat, fat2, 18u * 1024u)); /* writer path mirrors, too */
    /* R-07: replacing a file with an empty one releases its chain and
     * stores first=0 size=0. STREAM took freed cluster 3; NEW.TXT keeps 4;
     * TMP gets 5. */
    assert(!fat[10] && !fat[11]); /* cluster 5 free before TMP */
    assert(ss_storage_store(&volume.storage, "TEST/TMP", msg, 5, 0) == 0);
    assert(fat[10] == 0xff && fat[11] == 0xff); /* TMP got cluster 5 */
    assert(ss_storage_store(&volume.storage, "TEST/TMP", NULL, 0, 1) == 0);
    assert(!fat[10] && !fat[11]); /* cluster 5 released again */
    assert(!dir[160 + 26] && !dir[160 + 27]);
    for (unsigned k = 160 + 28; k < 160 + 32; k++) assert(!dir[k]);
    assert(ss_storage_read(&volume.storage, "TEST/TMP", out, 8) == 0);
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
    /* F-03: a failure while writing the second FAT copy mid-fat_set fails
     * the store and sets the fence. FAT1 was already updated, so the copies
     * then disagree until external inspection: per-copy updates, no atomic
     * volume swap. */
    memset(&volume, 0, sizeof(volume));
    assert(ss_scsi_volume_attach(&volume, &device) == 0);
    assert(!memcmp(fat, fat2, 18u * 1024u));
    fail_lba = (32u + 1u + 18u) * 2u; /* first physical block of FAT2 */
    assert(ss_storage_store(&volume.storage, "TEST/FAILFAT2", msg, 5, 0) != 0);
    assert(volume.storage.write_failed);
    fail_lba = -1;
    assert(ss_storage_store(&volume.storage, "TEST/BLOCKED", msg, 5, 0) != 0);
    assert(memcmp(fat, fat2, 18u * 1024u) != 0);
    memcpy(fat, saved_fat, sizeof(saved_fat));
    memcpy(fat2, saved_fat, sizeof(saved_fat));
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
    memcpy(fat2, fat, 18u * 1024u); /* keep both copies identical for attach */
    ipl[0x15] |= 0x80;
    memset(&volume, 0, sizeof(volume));
    assert(ss_scsi_volume_attach(&volume, &device) == 0);
    assert(ss_storage_read(&volume.storage, "TEST/STREAM", out, 8) == 5 && !memcmp(out, msg, 5));
    assert(ss_storage_store(&volume.storage, "TEST/INTEL", msg, 5, 0) == 0);
    assert(ss_storage_read(&volume.storage, "TEST/INTEL", out, 8) == 5 && !memcmp(out, msg, 5));
    /* Human68k stores the case a file was created with (files copied in from
     * another drive commonly end up lowercase on a real disk); lookup must
     * match names case-insensitively like Human68k DOS does. */
    for (unsigned i = 0; i < 1024; i += 32)
        if (!memcmp(dir + i, "INTEL      ", 11)) {
            for (unsigned k = 0; k < 11; k++)
                if (dir[i + k] >= 'A' && dir[i + k] <= 'Z')
                    dir[i + k] = (uint8_t)(dir[i + k] - 'A' + 'a');
            break;
        }
    assert(ss_storage_read(&volume.storage, "TEST/INTEL", out, 8) == 5 &&
           !memcmp(out, msg, 5));
    assert(ss_storage_store(&volume.storage, "TEST/intel",
                            (const uint8_t*)"WORLD", 5, 1) == 0);
    assert(ss_storage_read(&volume.storage, "TEST/INTEL", out, 8) == 5 &&
           !memcmp(out, "WORLD", 5));
    /* F-02: real Human68k media keep case flags in reserved bytes 0x0C-0x0D
     * and the 18+3 extended name in 0x0E-0x15. Case flags alone must not
     * hide an entry; extended-name bytes must (layout pending X-03). */
    unsigned intel_off = 0;
    for (unsigned i = 0; i < 1024; i += 32)
        if (!memcmp(dir + i, "intel      ", 11)) { intel_off = i; break; }
    assert(intel_off);
    dir[intel_off + 12] = 0x08; /* reverse-engineered case-flag values */
    dir[intel_off + 13] = 0x10;
    SSFatLocation loc;
    assert(ss_fat_locate(&volume.storage.volume, "TEST/INTEL", &loc) == 0 &&
           loc.found);
    assert(ss_storage_read(&volume.storage, "TEST/INTEL", out, 8) == 5 &&
           !memcmp(out, "WORLD", 5));
    uint8_t* ghost = dir + intel_off + 32;
    memcpy(ghost, "GHOST   TXT", 11);
    ghost[11] = 0x20;
    ghost[14] = 1; ghost[15] = 2; /* 18+3 extended name present: invisible */
    assert(ss_fat_locate(&volume.storage.volume, "TEST/GHOST.TXT", &loc) == 0 &&
           !loc.found);
    assert(ss_storage_read(&volume.storage, "TEST/GHOST.TXT", out, 8) == -1);
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
