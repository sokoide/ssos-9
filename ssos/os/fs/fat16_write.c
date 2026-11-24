#include "fat12.h"

#include <stddef.h>

/* Read one FAT16 entry. The caller supplies the FAT scratch buffer and its
 * cached sector index (UINT32_MAX = nothing loaded). */
static int fat_scan_get(SSFat12* v, uint16_t cluster, uint16_t* value,
                        uint8_t* fat, uint32_t* loaded) {
    uint32_t pos = (uint32_t)cluster * 2u;
    uint32_t index = pos / v->sector_size;
    if (index >= v->fat_sectors) return -1;
    if (index != *loaded) {
        if (v->read_sector(v->context, v->fat_start + index, fat)) return -1;
        *loaded = index;
    }
    pos %= v->sector_size;
    *value = (uint16_t)(fat[pos] | ((uint16_t)fat[pos + 1] << 8));
    if (v->fat_big_endian) *value = (uint16_t)((*value << 8) | (*value >> 8));
    return 0;
}

static int fat_get(SSFat12* v, uint16_t cluster, uint16_t* value) {
    uint32_t loaded = UINT32_MAX;
    return fat_scan_get(v, cluster, value, ss_fat_fat_scratch(), &loaded);
}

/* Human68k reserves FAT2 as unused, so only FAT1 carries the chain. */
static int fat_set(SSFat12* v, SSFatWriteSector write, uint16_t cluster,
                   uint16_t value) {
    uint8_t* fat = ss_fat_fat_scratch();
    uint32_t pos = (uint32_t)cluster * 2u;
    uint32_t index = pos / v->sector_size;
    if (index >= v->fat_sectors) return -1;
    pos %= v->sector_size;
    if (v->read_sector(v->context, v->fat_start + index, fat)) return -1;
    fat[pos] = (uint8_t)(v->fat_big_endian ? value >> 8 : value);
    fat[pos + 1] = (uint8_t)(v->fat_big_endian ? value : value >> 8);
    return write(v->context, v->fat_start + index, fat);
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

/* Take the first free cluster after the one taken last (wrapping once). */
static int writer_allocate(SSFat12* v, SSFatWriteSector write,
                           SSFat16Writer* writer) {
    uint32_t limit = (uint32_t)v->cluster_count + 2u;
    if (limit > 0xfff0u) limit = 0xfff0u;
    uint32_t start = writer->last ? (uint32_t)writer->last + 1u : 2u;
    if (start >= limit) start = 2;
    uint32_t c = start, loaded = UINT32_MAX;
    do {
        uint16_t value;
        if (fat_scan_get(v, (uint16_t)c, &value, ss_fat_fat_scratch(), &loaded))
            return -1;
        if (!value) {
            if (fat_set(v, write, (uint16_t)c, 0xffffu)) return -1;
            /* fat_set reloads the scratch with the sector it wrote. */
            loaded = UINT32_MAX;
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

static int write_file(SSFat12* v, SSFatWriteSector write, const char* name,
                      const uint8_t* data, uint32_t size, int replace) {
    SSFat16Writer writer;
    if (ss_fat16_writer_begin(v, name, replace, &writer)) return -1;
    if (ss_fat16_writer_write(v, write, &writer, data, size) != (int32_t)size) {
        (void)ss_fat16_writer_abort(v, write, &writer);
        return -1;
    }
    return ss_fat16_writer_commit(v, write, &writer);
}

int ss_fat16_create(SSFat12* v, SSFatWriteSector write, const char* name,
                    const uint8_t* data, uint32_t size) {
    return write_file(v, write, name, data, size, 0);
}

int ss_fat16_replace(SSFat12* v, SSFatWriteSector write, const char* name,
                     const uint8_t* data, uint32_t size) {
    return write_file(v, write, name, data, size, 1);
}

int ss_fat16_writer_begin(SSFat12* v, const char* name, int replace,
                          SSFat16Writer* writer) {
    SSFatLocation loc;
    SSFat16Writer result = {0};
    if (!v || !v->human68k || !writer || (replace != 0 && replace != 1) ||
        ss_fat_locate(v, name, &loc) ||
        !!loc.found != !!replace || (loc.found && (loc.entry[11] & 0x19u)))
        return -1;
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

int32_t ss_fat16_writer_write(SSFat12* v, SSFatWriteSector write,
                              SSFat16Writer* writer, const uint8_t* data,
                              uint32_t size) {
    uint8_t* sector = ss_fat_data_scratch();
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
    uint8_t* sector = ss_fat_data_scratch();
    if (!v || !write || !writer || !writer->active) return -1;
    /* A commit without any write owns no cluster (first=0, size=0),
     * matching the FAT convention for empty files. Replacing a file with an
     * empty one therefore releases its whole chain. (Offset-based truncation
     * does not exist in this writer.) */
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
    /* Once submitted, a failing directory write may still publish the chain. */
    if (write(v->context, writer->human_sector, sector)) return -1;
    return writer->replacing ? free_chain(v, write, writer->old_cluster) : 0;
}
