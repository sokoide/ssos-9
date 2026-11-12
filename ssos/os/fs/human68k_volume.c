#include "human68k_volume.h"

#include <stddef.h>

static uint16_t be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static int equal(const uint8_t* data, const char* text, unsigned n) {
    for (unsigned i = 0; i < n; i++) if (data[i] != (uint8_t)text[i]) return 0;
    return 1;
}

int ss_human68k_probe(SSHuman68kVolume* out, SSHuman68kReadSector read,
                      void* context, uint32_t disk_sectors, uint8_t partition) {
    uint8_t block[1024] __attribute__((aligned(4)));
    SSHuman68kVolume v = {0};
    if (!out || !read || disk_sectors < 4 || partition >= 15 ||
        read(context, 0, block) || !equal(block, "X68SCSI1", 8) ||
        read(context, 2, block) || !equal(block, "X68K", 4)) return -1;
    const uint8_t* entry = block + 16u + (uint32_t)partition * 16u;
    if (!equal(entry, "Human68k", 8)) return -1;
    uint32_t encoded = be32(entry + 8);
    uint8_t flags = (uint8_t)(encoded >> 24);
    v.start = encoded & 0x00ffffffu;
    v.sectors = be32(entry + 12);
    if ((flags != 0 && flags != 2) || v.start < 3 || !v.sectors ||
        v.start >= disk_sectors || v.sectors > disk_sectors - v.start ||
        read(context, v.start, block)) return -1;
    v.sector_size = be16(block + 0x12);
    v.sectors_per_cluster = block[0x14];
    v.fat_count = block[0x15] & 0x7f;
    v.fat_little_endian = !!(block[0x15] & 0x80);
    v.fat_start = be16(block + 0x16);
    v.root_entries = be16(block + 0x18);
    uint32_t total = be16(block + 0x1a);
    if (!total) total = be32(block + 0x1e);
    v.fat_sectors = block[0x1d];
    uint32_t spc = v.sectors_per_cluster;
    if (v.sector_size != 1024 || !spc || (spc & (spc - 1)) ||
        !v.fat_count || !v.fat_start || !v.fat_sectors || !v.root_entries ||
        !total || total > v.sectors || be32(block + 0x22) != v.start)
        return -1;
    v.sectors = total;
    v.root_start = v.fat_start + (uint32_t)v.fat_count * v.fat_sectors;
    v.data_start = v.root_start + ((uint32_t)v.root_entries * 32u + 1023u) / 1024u;
    if (v.data_start >= total) return -1;
    v.clusters = (total - v.data_start) / spc;
    if (v.clusters < 4085 || v.clusters >= 65525 ||
        (v.clusters + 2u) * 2u > (uint32_t)v.fat_sectors * 1024u)
        return -1;
    *out = v;
    return 0;
}
