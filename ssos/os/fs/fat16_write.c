#include "fat12.h"

#include <stddef.h>

static int make_name(const char* name, uint8_t out[11]) {
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

static int fat_get(SSFat12* v, uint16_t cluster, uint16_t* value) {
    uint8_t sector[1024] __attribute__((aligned(4)));
    uint32_t pos = (uint32_t)cluster * 2u;
    if (pos / v->sector_size >= v->fat_sectors ||
        v->read_sector(v->context, v->fat_start + pos / v->sector_size, sector))
        return -1;
    pos %= v->sector_size;
    *value = (uint16_t)(sector[pos] | ((uint16_t)sector[pos + 1] << 8));
    if (v->fat_big_endian) *value = (uint16_t)((*value << 8) | (*value >> 8));
    return 0;
}

static int fat_set(SSFat12* v, SSFatWriteSector write, uint16_t cluster,
                   uint16_t value) {
    uint8_t sector[1024] __attribute__((aligned(4)));
    uint32_t pos = (uint32_t)cluster * 2u;
    uint32_t index = pos / v->sector_size;
    if (index >= v->fat_sectors) return -1;
    pos %= v->sector_size;
    for (uint32_t copy = 0; copy < v->fat_count; copy++) {
        uint32_t at = v->fat_start + copy * v->fat_sectors + index;
        if (v->read_sector(v->context, at, sector)) return -1;
        sector[pos] = (uint8_t)(v->fat_big_endian ? value >> 8 : value);
        sector[pos + 1] = (uint8_t)(v->fat_big_endian ? value : value >> 8);
        if (write(v->context, at, sector)) return -1;
    }
    return 0;
}

static int free_chain(SSFat12* v, SSFatWriteSector write, uint16_t cluster) {
    for (uint32_t n = 0; cluster >= 2 && cluster < 0xfff0u &&
                         cluster < (uint32_t)v->cluster_count + 2u &&
                         n < v->cluster_count; n++) {
        uint16_t next;
        if (fat_get(v, cluster, &next) || fat_set(v, write, cluster, 0)) return -1;
        cluster = next;
    }
    return 0;
}

static int has_free_clusters(SSFat12* v, uint32_t needed, uint8_t* sector) {
    uint32_t loaded = UINT32_MAX;
    for (uint32_t cluster = 2; needed && cluster < (uint32_t)v->cluster_count + 2u &&
                               cluster < 0xfff0u; cluster++) {
        uint32_t pos = cluster * 2u;
        uint32_t index = pos / v->sector_size;
        if (index >= v->fat_sectors) return -1;
        if (index != loaded) {
            if (v->read_sector(v->context, v->fat_start + index, sector)) return -1;
            loaded = index;
        }
        pos %= v->sector_size;
        if (!sector[pos] && !sector[pos + 1u]) needed--;
    }
    return needed ? -1 : 0;
}

/* These legacy buffers must not share the 4 KiB task stack with Human68k lookup. */
static __attribute__((noinline)) int write_file_legacy(SSFat12* v, SSFatWriteSector write, const char* name,
                      const uint8_t* data, uint32_t size, int replace) {
    uint8_t sector[1024] __attribute__((aligned(4)));
    uint8_t short_name[11];
    if (!v || !v->read_sector || !write || v->fat_bits != 16 ||
        (!data && size) || make_name(name, short_name)) return -1;
    uint32_t slot = UINT32_MAX;
    int slot_was_end = 0;
    uint16_t old_cluster = 0;
    int found = 0;
    uint32_t per_sector = v->sector_size / 32u;
    for (uint32_t i = 0; i < v->root_entries; i++) {
        if (i % per_sector == 0 &&
            v->read_sector(v->context, v->root_start + i / per_sector, sector))
            return -1;
        uint8_t* entry = sector + (i % per_sector) * 32u;
        if (!entry[0] || entry[0] == 0xe5) {
            if (slot == UINT32_MAX) {
                slot = i;
                slot_was_end = entry[0] == 0;
            }
            if (!entry[0]) break;
            continue;
        }
        unsigned j = 0;
        while (j < 11 && entry[j] == short_name[j]) j++;
        if (j == 11) {
            if (!replace || (entry[11] & 0x19u)) return -1;
            SSFat12File existing;
            if (ss_fat12_open(v, name, &existing)) return -1;
            if (existing.size == 0 && existing.cursor_cluster != 0) return -1;
            old_cluster = existing.cursor_cluster;
            slot = i;
            slot_was_end = 0;
            found = 1;
            break;
        }
    }
    if (slot == UINT32_MAX || (replace && !found)) return -1;

    uint32_t cluster_bytes = (uint32_t)v->sector_size * v->sectors_per_cluster;
    uint32_t needed = size / cluster_bytes + (size % cluster_bytes != 0);
    if (needed > v->cluster_count || has_free_clusters(v, needed, sector)) return -1;
    uint16_t first = 0, last = 0;
    uint32_t offset = 0, allocated = 0;
    for (uint32_t c = 2; c < (uint32_t)v->cluster_count + 2u &&
                         c < 0xfff0u && allocated < needed; c++) {
        uint16_t value;
        if (fat_get(v, (uint16_t)c, &value)) goto fail;
        if (value) continue;
        if (fat_set(v, write, (uint16_t)c, 0xffffu)) goto fail;
        if (last && fat_set(v, write, last, (uint16_t)c)) {
            (void)fat_set(v, write, (uint16_t)c, 0);
            goto fail;
        }
        if (!first) first = (uint16_t)c;
        last = (uint16_t)c;
        allocated++;
        for (uint32_t s = 0; s < v->sectors_per_cluster; s++) {
            for (uint32_t i = 0; i < v->sector_size; i++)
                sector[i] = offset < size ? data[offset++] : 0;
            uint32_t at = v->data_start + (c - 2u) * v->sectors_per_cluster + s;
            if (at >= v->total_sectors || write(v->context, at, sector)) goto fail;
        }
    }
    if (allocated != needed) goto fail;
    uint32_t at = v->root_start + slot / per_sector;
    if (slot_was_end && slot + 1u < v->root_entries &&
        (slot + 1u) % per_sector == 0) {
        if (v->read_sector(v->context, at + 1u, sector)) goto fail;
        sector[0] = 0;
        if (write(v->context, at + 1u, sector)) goto fail;
    }
    if (v->read_sector(v->context, at, sector)) goto fail;
    uint8_t* entry = sector + (slot % per_sector) * 32u;
    if (slot_was_end && slot + 1u < v->root_entries &&
        (slot + 1u) % per_sector != 0)
        entry[32] = 0;
    if (!found) {
        for (unsigned i = 0; i < 32; i++) entry[i] = 0;
        for (unsigned i = 0; i < 11; i++) entry[i] = short_name[i];
    }
    entry[11] |= 0x20;
    entry[26] = (uint8_t)first;
    entry[27] = (uint8_t)(first >> 8);
    for (unsigned i = 0; i < 4; i++) entry[28 + i] = (uint8_t)(size >> (i * 8));
    /* A failed write may still have reached the disk: never free the chain
     * after the directory entry has been submitted. */
    if (write(v->context, at, sector)) return -1;
    return found ? free_chain(v, write, old_cluster) : 0;
fail:
    (void)free_chain(v, write, first);
    return -1;
}

static int write_file(SSFat12* v, SSFatWriteSector write, const char* name,
                      const uint8_t* data, uint32_t size, int replace) {
    if (v && v->human68k) {
        SSFat16Writer writer;
        if (ss_fat16_writer_begin(v, name, replace, &writer)) return -1;
        if (ss_fat16_writer_write(v, write, &writer, data, size) != (int32_t)size) {
            (void)ss_fat16_writer_abort(v, write, &writer);
            return -1;
        }
        return ss_fat16_writer_commit(v, write, &writer);
    }
    return write_file_legacy(v, write, name, data, size, replace);
}

int ss_fat16_create(SSFat12* v, SSFatWriteSector write, const char* name,
                    const uint8_t* data, uint32_t size) {
    return write_file(v, write, name, data, size, 0);
}

int ss_fat16_replace(SSFat12* v, SSFatWriteSector write, const char* name,
                     const uint8_t* data, uint32_t size) {
    return write_file(v, write, name, data, size, 1);
}

static __attribute__((noinline)) int writer_begin_legacy(SSFat12* v, const char* name, int replace,
                          SSFat16Writer* writer) {
    uint8_t sector[1024] __attribute__((aligned(4)));
    uint8_t short_name[11];
    if (!v || !writer || v->fat_bits != 16 || !v->read_sector ||
        (replace != 0 && replace != 1) || make_name(name, short_name)) return -1;
    SSFat16Writer result = {0};
    result.slot = UINT32_MAX;
    uint32_t per_sector = v->sector_size / 32u;
    for (uint32_t i = 0; i < v->root_entries; i++) {
        if (i % per_sector == 0 &&
            v->read_sector(v->context, v->root_start + i / per_sector, sector))
            return -1;
        uint8_t* entry = sector + (i % per_sector) * 32u;
        if (!entry[0] || entry[0] == 0xe5) {
            if (result.slot == UINT32_MAX) {
                result.slot = i;
                result.slot_was_end = entry[0] == 0;
            }
            if (!entry[0]) break;
            continue;
        }
        unsigned j = 0;
        while (j < 11 && entry[j] == short_name[j]) j++;
        if (j == 11) {
            if (!replace || (entry[11] & 0x19u)) return -1;
            SSFat12File existing;
            if (ss_fat12_open(v, name, &existing) ||
                (!existing.size && existing.cursor_cluster)) return -1;
            result.old_cluster = existing.cursor_cluster;
            result.slot = i;
            result.slot_was_end = 0;
            result.replacing = 1;
            break;
        }
    }
    if (result.slot == UINT32_MAX || (replace && !result.replacing)) return -1;
    for (unsigned i = 0; i < 11; i++) result.name[i] = short_name[i];
    result.active = 1;
    *writer = result;
    return 0;
}

int ss_fat16_writer_begin(SSFat12* v, const char* name, int replace,
                          SSFat16Writer* writer) {
    if (v && v->human68k) {
        SSFatLocation loc;
        SSFat16Writer result = {0};
        if (!writer || (replace != 0 && replace != 1) || ss_fat_locate(v, name, &loc) ||
            !!loc.found != !!replace || (loc.found && (loc.entry[11] & 0x19u))) return -1;
        if (loc.found) {
            SSFat12File existing;
            if (ss_fat12_open(v, name, &existing)) return -1;
            result.old_cluster = existing.cursor_cluster;
        }
        result.human_sector = loc.sector; result.human_offset = loc.offset;
        result.human_next_sector = loc.next_sector;
        result.human_has_next_sector = loc.has_next_sector;
        result.slot_was_end = loc.was_end; result.replacing = (uint8_t)replace;
        for (unsigned i = 0; i < 11; i++) result.name[i] = loc.name[i];
        result.active = 1; *writer = result;
        return 0;
    }
    return writer_begin_legacy(v, name, replace, writer);
}

static int writer_allocate(SSFat12* v, SSFatWriteSector write,
                           SSFat16Writer* writer) {
    uint32_t limit = (uint32_t)v->cluster_count + 2u;
    if (limit > 0xfff0u) limit = 0xfff0u;
    uint32_t start = writer->last ? (uint32_t)writer->last + 1u : 2u;
    if (start >= limit) start = 2;
    uint32_t c = start;
    do {
        uint16_t value;
        if (fat_get(v, (uint16_t)c, &value)) return -1;
        if (!value) {
            if (fat_set(v, write, (uint16_t)c, 0xffffu)) return -1;
            if (writer->last && fat_set(v, write, writer->last, (uint16_t)c)) {
                (void)fat_set(v, write, (uint16_t)c, 0);
                return -1;
            }
            if (!writer->first) writer->first = (uint16_t)c;
            writer->last = (uint16_t)c;
            return 0;
        }
        if (++c >= limit) c = 2;
    } while (c != start);
    return -1;
}

int32_t ss_fat16_writer_write(SSFat12* v, SSFatWriteSector write,
                              SSFat16Writer* writer, const uint8_t* data,
                              uint32_t size) {
    uint8_t sector[1024] __attribute__((aligned(4)));
    if (!v || !write || !writer || !writer->active || (!data && size) ||
        size > 0x7fffffffu || size > UINT32_MAX - writer->size) return -1;
    uint32_t cluster_bytes = (uint32_t)v->sector_size * v->sectors_per_cluster;
    uint32_t done = 0;
    while (done < size) {
        uint32_t within_cluster = writer->size % cluster_bytes;
        if (!within_cluster && writer_allocate(v, write, writer)) return -1;
        uint32_t within_sector = writer->size % v->sector_size;
        uint32_t at = v->data_start + ((uint32_t)writer->last - 2u) *
                      v->sectors_per_cluster + within_cluster / v->sector_size;
        if (at >= v->total_sectors) return -1;
        if (within_sector) {
            if (v->read_sector(v->context, at, sector)) return -1;
        } else {
            for (uint32_t i = 0; i < v->sector_size; i++) sector[i] = 0;
        }
        uint32_t count = v->sector_size - within_sector;
        if (count > size - done) count = size - done;
        for (uint32_t i = 0; i < count; i++) sector[within_sector + i] = data[done + i];
        if (write(v->context, at, sector)) return -1;
        writer->size += count;
        done += count;
    }
    return (int32_t)done;
}

int ss_fat16_writer_abort(SSFat12* v, SSFatWriteSector write,
                          SSFat16Writer* writer) {
    if (!v || !write || !writer || !writer->active) return -1;
    writer->active = 0;
    return free_chain(v, write, writer->first);
}

int ss_fat16_writer_commit(SSFat12* v, SSFatWriteSector write,
                           SSFat16Writer* writer) {
    uint8_t sector[1024] __attribute__((aligned(4)));
    if (!v || !write || !writer || !writer->active) return -1;
    if (v->human68k) {
        /* A commit without any write owns no cluster (first=0, size=0),
         * matching the legacy path and the FAT convention for empty files.
         * Replacing a file with an empty one therefore releases its whole
         * chain. (Offset-based truncation does not exist in this writer.) */
        if (!writer->first && writer->size && writer_allocate(v, write, writer))
            return -1;
        if (writer->slot_was_end && writer->human_has_next_sector) {
            if (v->read_sector(v->context, writer->human_next_sector, sector)) return -1;
            sector[0] = 0;
            if (write(v->context, writer->human_next_sector, sector)) return -1;
        }
        if (v->read_sector(v->context, writer->human_sector, sector)) return -1;
        uint8_t* entry = sector + writer->human_offset;
        if (writer->slot_was_end && writer->human_offset + 32u < v->sector_size) entry[32] = 0;
        if (!writer->replacing) {
            for (unsigned i = 0; i < 32; i++) entry[i] = 0;
            for (unsigned i = 0; i < 11; i++) entry[i] = writer->name[i];
        }
        entry[11] |= 0x20;
        entry[26] = (uint8_t)writer->first;
        entry[27] = (uint8_t)(writer->first >> 8);
        for (unsigned i = 0; i < 4; i++) entry[28 + i] = (uint8_t)(writer->size >> (i * 8));
        writer->active = 0;
        if (write(v->context, writer->human_sector, sector)) return -1;
        return writer->replacing ? free_chain(v, write, writer->old_cluster) : 0;
    }
    uint32_t per_sector = v->sector_size / 32u;
    uint32_t at = v->root_start + writer->slot / per_sector;
    if (writer->slot_was_end && writer->slot + 1u < v->root_entries &&
        (writer->slot + 1u) % per_sector == 0) {
        if (v->read_sector(v->context, at + 1u, sector)) return -1;
        sector[0] = 0;
        if (write(v->context, at + 1u, sector)) return -1;
    }
    if (v->read_sector(v->context, at, sector)) return -1;
    uint8_t* entry = sector + (writer->slot % per_sector) * 32u;
    if (writer->slot_was_end && writer->slot + 1u < v->root_entries &&
        (writer->slot + 1u) % per_sector != 0) entry[32] = 0;
    if (!writer->replacing) {
        for (unsigned i = 0; i < 32; i++) entry[i] = 0;
        for (unsigned i = 0; i < 11; i++) entry[i] = writer->name[i];
    }
    entry[11] |= 0x20;
    entry[26] = (uint8_t)writer->first;
    entry[27] = (uint8_t)(writer->first >> 8);
    for (unsigned i = 0; i < 4; i++) entry[28 + i] = (uint8_t)(writer->size >> (i * 8));
    /* Once submitted, a failing directory write may still publish the chain. */
    writer->active = 0;
    if (write(v->context, at, sector)) return -1;
    return writer->replacing ? free_chain(v, write, writer->old_cluster) : 0;
}
