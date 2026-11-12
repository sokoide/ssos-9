#include "../../ssos/os/fs/human68k_volume.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* XEiJ SCMedia layout: logical sectors are 1024 bytes regardless of the
 * physical SCSI record size. The test reader is deliberately sparse. */
typedef struct {
    uint8_t id[1024], table[1024], ipl[1024];
    int reads;
} Fixture;

static void be16(uint8_t* p, uint16_t n) { p[0] = n >> 8; p[1] = n; }
static void be32(uint8_t* p, uint32_t n) {
    p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n;
}
static int read_sector(void* context, uint32_t sector, uint8_t* out) {
    Fixture* f = context;
    f->reads++;
    const uint8_t* src = sector == 0 ? f->id : sector == 2 ? f->table :
                         sector == 32 ? f->ipl : NULL;
    if (!src) return -1;
    memcpy(out, src, 1024);
    return 0;
}
static void init(Fixture* f) {
    memset(f, 0, sizeof(*f));
    memcpy(f->id, "X68SCSI1", 8);
    be16(f->id + 8, 512);
    be32(f->id + 10, 20000);
    memcpy(f->table, "X68K", 4);
    be32(f->table + 4, 10000);
    be32(f->table + 8, 10000);
    be32(f->table + 12, 10000);
    memcpy(f->table + 16, "Human68k", 8);
    be32(f->table + 24, 32);
    be32(f->table + 28, 9000);
    be16(f->ipl + 0x12, 1024);
    f->ipl[0x14] = 1;
    f->ipl[0x15] = 2;
    be16(f->ipl + 0x16, 1);
    be16(f->ipl + 0x18, 512);
    f->ipl[0x1c] = 0xf7;
    f->ipl[0x1d] = 18;
    be32(f->ipl + 0x1e, 9000);
    be32(f->ipl + 0x22, 32);
}

int main(void) {
    Fixture f;
    SSHuman68kVolume v;
    init(&f);
    assert(ss_human68k_probe(&v, read_sector, &f, 10000, 0) == 0);
    assert(v.start == 32 && v.sectors == 9000 && v.fat_start == 1);
    assert(v.root_start == 37 && v.data_start == 53 && v.clusters == 8947);
    assert(v.fat_sectors == 18 && v.fat_count == 2 && v.sector_size == 1024);
    assert(v.fat_little_endian == 0);
    f.ipl[0x15] |= 0x80;
    assert(ss_human68k_probe(&v, read_sector, &f, 10000, 0) == 0);
    assert(v.fat_little_endian == 1);
    init(&f); f.table[0] = 0;
    assert(ss_human68k_probe(&v, read_sector, &f, 10000, 0) != 0);
    init(&f); be32(f.table + 28, 10000);
    assert(ss_human68k_probe(&v, read_sector, &f, 10000, 0) != 0);
    init(&f); be32(f.table + 24, 0x01000020);
    assert(ss_human68k_probe(&v, read_sector, &f, 10000, 0) != 0);
    init(&f); f.ipl[0x1d] = 1;
    assert(ss_human68k_probe(&v, read_sector, &f, 10000, 0) != 0);
    init(&f); be16(f.ipl + 0x12, 512);
    assert(ss_human68k_probe(&v, read_sector, &f, 10000, 0) != 0);
    init(&f);
    assert(ss_human68k_probe(&v, read_sector, &f, 10000, 15) != 0);
    puts("Human68k SCSI volume tests passed");
    return 0;
}
