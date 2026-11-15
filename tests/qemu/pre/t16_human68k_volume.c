#include "../../../ssos/os/fs/human68k_volume.h"
#include "../../../ssos/os/fs/scsi_volume.h"
#include "tty.h"

#include <stdint.h>

static void put16(uint8_t* p, uint16_t n) { p[0] = n >> 8; p[1] = n; }
static void put32(uint8_t* p, uint32_t n) {
    p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n;
}
static uint8_t blocks[128][1024];
static int scsi_read(int lba, int count, int target, int code, void* out) {
    if (lba < 0 || lba >= 256 || count != 1 || target || code != 1) return -1;
    for (unsigned i = 0; i < 512; i++) ((uint8_t*)out)[i] = blocks[lba / 2][(lba % 2) * 512 + i];
    return 0;
}
static int scsi_write(int lba, int count, int target, int code, const void* in) {
    if (lba < 0 || lba >= 256 || count != 1 || target || code != 1) return -1;
    for (unsigned i = 0; i < 512; i++) blocks[lba / 2][(lba % 2) * 512 + i] = ((const uint8_t*)in)[i];
    return 0;
}
static int read_sector(void* context, uint32_t sector, uint8_t* out) {
    (void)context;
    for (unsigned i = 0; i < 1024; i++) out[i] = 0;
    if (sector == 0) {
        const char* id = "X68SCSI1";
        for (unsigned i = 0; i < 8; i++) out[i] = id[i];
    } else if (sector == 2) {
        const char* id = "X68K"; const char* name = "Human68k";
        for (unsigned i = 0; i < 4; i++) out[i] = id[i];
        for (unsigned i = 0; i < 8; i++) out[16 + i] = name[i];
        put32(out + 24, 32); put32(out + 28, 9000);
    } else if (sector == 32) {
        put16(out + 0x12, 1024); out[0x14] = 1; out[0x15] = 2;
        put16(out + 0x16, 1); put16(out + 0x18, 512);
        out[0x1c] = 0xf7; out[0x1d] = 18;
        put32(out + 0x1e, 9000); put32(out + 0x22, 32);
    } else return -1;
    return 0;
}
int main(void) {
    SSHuman68kVolume v;
    if (ss_human68k_probe(&v, read_sector, 0, 10000, 0) ||
        v.start != 32 || v.data_start != 53 || v.clusters != 8947) {
        tty_puts("FAIL Human68k volume\n");
        for (;;) {}
    }
    if (read_sector(0, 0, blocks[0]) || read_sector(0, 2, blocks[2]) ||
        read_sector(0, 32, blocks[32])) { tty_puts("FAIL fixture\n"); for (;;) {} }
    uint8_t* fat = blocks[33];
    fat[0] = 0xf7; fat[1] = fat[2] = fat[3] = fat[4] = fat[5] = 0xff;
    const char* name = "TEST       ";
    for (unsigned i = 0; i < 11; i++) blocks[69][i] = name[i];
    blocks[69][11] = 0x10; blocks[69][26] = 2;
    SSScsiDevice d = {scsi_read, scsi_write, 20000, 0, 20000, 512, 0};
    SSScsiVolume volume = {0}; uint8_t out[4] = {0};
    /* FAT2 is unused and deliberately differs from FAT1. */
    if (ss_scsi_volume_attach(&volume, &d)) {
        tty_puts("FAIL Human68k attach rejected unused FAT2\n"); for (;;) {}
    }
    if (ss_storage_store(&volume.storage, "A:\\TEST\\A.TXT",
                         (const uint8_t*)"abc", 3, 0)) {
        tty_puts("FAIL Human68k store\n"); for (;;) {}
    }
    if (ss_storage_read(&volume.storage, "TEST/A.TXT", out, sizeof(out)) != 3 ||
        out[0] != 'a' || out[1] != 'b' || out[2] != 'c') {
        tty_puts("FAIL Human68k read\n"); for (;;) {}
    }
    /* The unused second FAT must remain untouched. */
    for (unsigned s = 0; s < 18; s++)
        for (unsigned i = 0; i < 1024; i++)
            if (blocks[51 + s][i]) {
                tty_puts("FAIL Human68k FAT2 modified\n"); for (;;) {}
            }
    tty_puts("OK Human68k volume and storage\n");
    for (;;) {}
}
