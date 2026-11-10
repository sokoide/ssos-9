#include "fat12.h"

#include <stddef.h>

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)le16(p) | ((uint32_t)le16(p + 2) << 16);
}

int ss_fat12_mount(SSFat12* v, SSFat12ReadSector read, void* context,
                   uint32_t available) {
    uint8_t sector[1024] __attribute__((aligned(4)));
    if (!v || !read || !available || read(context, 0, sector) != 0 ||
        sector[510] != 0x55 || sector[511] != 0xaa)
        return -1;
    uint32_t bps = le16(sector + 11);
    if (bps != 512 && bps != 1024) return -1;
    uint32_t reserved = le16(sector + 14), fats = sector[16];
    uint32_t entries = le16(sector + 17), fat_size = le16(sector + 22);
    uint32_t total = le16(sector + 19);
    if (!total) total = le32(sector + 32);
    uint32_t spc = sector[13];
    if (!reserved || !fats || !entries || !fat_size || !total ||
        total > available || !spc || spc > 128 || (spc & (spc - 1)))
        return -1;
    uint32_t root = reserved + fats * fat_size;
    uint32_t data = root + (entries * 32u + bps - 1u) / bps;
    if (data >= total) return -1;
    uint32_t clusters = (total - data) / spc;
    uint32_t bits = clusters < 4085u ? 12u : 16u;
    uint32_t fat_bytes = bits == 12u ? ((clusters + 2u) * 3u + 1u) / 2u
                                     : (clusters + 2u) * 2u;
    if (!clusters || clusters >= 65525u || fat_bytes > fat_size * bps)
        return -1;
    v->read_sector = read;
    v->context = context;
    v->total_sectors = total;
    v->fat_start = reserved;
    v->root_start = root;
    v->data_start = data;
    v->fat_sectors = (uint16_t)fat_size;
    v->root_entries = (uint16_t)entries;
    v->cluster_count = (uint16_t)clusters;
    v->sector_size = (uint16_t)bps;
    v->sectors_per_cluster = (uint8_t)spc;
    v->fat_bits = (uint8_t)bits;
    v->fat_count = (uint8_t)fats;
    return 0;
}

static int short_name(const char* name, uint8_t out[11]) {
    if (!name) return -1;
    for (unsigned i = 0; i < 11; i++) out[i] = ' ';
    unsigned base = 0, ext = 0;
    int dot = 0;
    for (; *name; name++) {
        unsigned char c = (unsigned char)*name;
        if (c == '.') {
            if (dot || !base) return -1;
            dot = 1;
            continue;
        }
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-')) return -1;
        if (dot) {
            if (ext == 3) return -1;
            out[8 + ext++] = c;
        } else {
            if (base == 8) return -1;
            out[base++] = c;
        }
    }
    return base && (!dot || ext) ? 0 : -1;
}

static int next_cluster(SSFat12* v, uint16_t cluster, uint16_t* next);

int ss_fat12_open(SSFat12* v, const char* name, SSFat12File* file) {
    uint8_t expected[11], sector[1024] __attribute__((aligned(4)));
    if (!v || !v->read_sector || !file || short_name(name, expected)) return -1;
    for (uint32_t i = 0; i < v->root_entries; i++) {
        uint32_t per_sector = v->sector_size / 32u;
        if (i % per_sector == 0 &&
            v->read_sector(v->context, v->root_start + i / per_sector, sector))
            return -1;
        const uint8_t* entry = sector + (i % per_sector) * 32u;
        if (!entry[0]) break;
        if (entry[0] == 0xe5 || (entry[11] & 0x18u)) continue;
        unsigned j = 0;
        while (j < 11 && entry[j] == expected[j]) j++;
        if (j != 11) continue;
        uint32_t size = le32(entry + 28);
        uint16_t cluster = le16(entry + 26);
        if (size && (cluster < 2 || cluster >= v->cluster_count + 2u)) return -1;
        uint32_t bytes_per_cluster = (uint32_t)v->sectors_per_cluster * v->sector_size;
        uint32_t needed = size / bytes_per_cluster + (size % bytes_per_cluster != 0);
        if (needed > v->cluster_count) return -1;
        /* A deterministic FAT chain that reaches EOC at exactly needed
         * clusters cannot contain a cycle; bounding traversal also avoids an
         * 8 KiB FAT16 bitmap on the small kernel stack. */
        uint16_t current = cluster;
        for (uint32_t n = 0; n < needed; n++) {
            if (current < 2 || current >= v->cluster_count + 2u ||
                current >= (v->fat_bits == 12 ? 0xff0u : 0xfff0u)) return -1;
            uint16_t next;
            if (next_cluster(v, current, &next)) return -1;
            if (n + 1u == needed) {
                if (next < (v->fat_bits == 12 ? 0xff8u : 0xfff8u)) return -1;
            } else {
                current = next;
            }
        }
        file->volume = v;
        file->size = size;
        file->offset = 0;
        file->cursor_cluster = cluster;
        file->cursor_index = 0;
        return 0;
    }
    return -1;
}

static int next_cluster(SSFat12* v, uint16_t cluster, uint16_t* next) {
    uint8_t sector[1024] __attribute__((aligned(4)));
    uint32_t bps = v->sector_size;
    uint32_t pos = v->fat_bits == 12 ? cluster + cluster / 2u : cluster * 2u;
    if (pos / bps >= v->fat_sectors ||
        v->read_sector(v->context, v->fat_start + pos / bps, sector)) return -1;
    uint16_t value = sector[pos % bps];
    pos++;
    if (!(pos % bps) &&
        (pos / bps >= v->fat_sectors ||
         v->read_sector(v->context, v->fat_start + pos / bps, sector)))
        return -1;
    value |= (uint16_t)sector[pos % bps] << 8;
    *next = v->fat_bits == 16 ? value :
            (cluster & 1u) ? value >> 4 : value & 0xfffu;
    return 0;
}

int32_t ss_fat12_read(void* context, uint8_t* out, uint32_t size) {
    SSFat12File* file = context;
    uint8_t sector[1024] __attribute__((aligned(4)));
    if (!file || !file->volume || (!out && size)) return -1;
    SSFat12* v = file->volume;
    if (file->offset > file->size) return -1;
    if (size > file->size - file->offset) size = file->size - file->offset;
    uint32_t done = 0, cluster_bytes = (uint32_t)v->sectors_per_cluster * v->sector_size;
    uint16_t cluster = file->cursor_cluster;
    uint32_t cluster_index = file->cursor_index;
    while (done < size) {
        uint32_t position = file->offset + done;
        uint32_t steps = position / cluster_bytes;
        if (steps >= v->cluster_count || steps < cluster_index) return -1;
        while (cluster_index < steps) {
            if (cluster < 2 || cluster >= v->cluster_count + 2u ||
                next_cluster(v, cluster, &cluster)) return -1;
            cluster_index++;
        }
        if (cluster < 2 || cluster >= v->cluster_count + 2u) return -1;
        uint32_t within = position % cluster_bytes;
        uint32_t logical = v->data_start + (cluster - 2u) * v->sectors_per_cluster + within / v->sector_size;
        if (logical >= v->total_sectors || v->read_sector(v->context, logical, sector)) return -1;
        uint32_t count = v->sector_size - within % v->sector_size;
        if (count > size - done) count = size - done;
        for (uint32_t i = 0; i < count; i++) out[done + i] = sector[within % v->sector_size + i];
        done += count;
    }
    file->offset += done;
    file->cursor_cluster = cluster;
    file->cursor_index = (uint16_t)cluster_index;
    return (int32_t)done;
}
