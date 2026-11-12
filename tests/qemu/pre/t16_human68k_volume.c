#include "../../../ssos/os/fs/human68k_volume.h"
#include "tty.h"

#include <stdint.h>

static void put16(uint8_t* p, uint16_t n) { p[0] = n >> 8; p[1] = n; }
static void put32(uint8_t* p, uint32_t n) {
    p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n;
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
    tty_puts("OK Human68k volume probe\n");
    for (;;) {}
}
