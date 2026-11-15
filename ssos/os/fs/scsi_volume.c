#include "scsi_volume.h"
#include "human68k_volume.h"

static int read1024(void* context, uint32_t sector, uint8_t* out) {
    SSScsiDevice* device = context;
    uint32_t factor = 1024u / device->block_size;
    if (sector >= device->volume_blocks / factor) return -1;
    for (uint32_t i = 0; i < factor; i++)
        if (ss_scsi_read_sector(device, sector * factor + i, out + i * device->block_size)) return -1;
    return 0;
}
static int write1024(void* context, uint32_t sector, const uint8_t* in) {
    SSScsiDevice* device = context;
    uint32_t factor = 1024u / device->block_size;
    if (sector >= device->volume_blocks / factor) return -1;
    for (uint32_t i = 0; i < factor; i++)
        if (ss_scsi_write_sector(device, sector * factor + i, in + i * device->block_size)) return -1;
    return 0;
}

int ss_scsi_volume_attach(SSScsiVolume* volume, const SSScsiDevice* device) {
    SSHuman68kVolume h;
    if (!volume || !device || volume->storage.mounted || !device->read || !device->write ||
        (device->block_size != 512 && device->block_size != 1024)) return -1;
    SSScsiDevice candidate = *device;
    uint32_t factor = 1024u / candidate.block_size;
    if (ss_human68k_probe(&h, read1024, &candidate, candidate.volume_blocks / factor, 0)) return -1;
    if (h.start > candidate.volume_blocks / factor ||
        h.sectors > candidate.volume_blocks / factor - h.start) return -1;
    candidate.volume_start += h.start * factor;
    candidate.volume_blocks = h.sectors * factor;
    uint8_t fat_header[1024] __attribute__((aligned(4)));
    if (read1024(&candidate, h.fat_start, fat_header) ||
        fat_header[0] != 0xf7 || fat_header[1] != 0xff ||
        fat_header[2] != 0xff || fat_header[3] != 0xff) return -1;
    SSStorage storage = {0};
    SSFat12* v = &storage.volume;
    /* Validate against the candidate so a rejected volume leaves the output
     * untouched, then re-point the reader at the owned device copy. */
    v->read_sector = read1024; v->context = &candidate;
    v->total_sectors = h.sectors; v->fat_start = h.fat_start;
    v->root_start = h.root_start; v->data_start = h.data_start;
    v->fat_sectors = h.fat_sectors; v->root_entries = h.root_entries;
    v->cluster_count = (uint16_t)h.clusters; v->sector_size = 1024;
    v->sectors_per_cluster = h.sectors_per_cluster; v->fat_bits = 16;
    v->fat_count = h.fat_count; v->human68k = 1; v->fat_big_endian = !h.fat_little_endian;
    /* Same reject-on-mismatch rule as ss_storage_mount: a writer updates
     * every FAT copy, so a volume where Human68k already left FAT1 != FAT2
     * must not be write-mounted. */
    if (ss_storage_fat_copies_match(v)) return -1;
    v->context = &volume->device;
    storage.write_sector = write1024; storage.mounted = 1;
    volume->device = candidate; volume->storage = storage;
    return 0;
}
