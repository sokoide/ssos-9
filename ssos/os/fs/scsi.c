#include "scsi.h"

#include <stddef.h>

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

int ss_scsi_configure(SSScsiDevice* device, uint8_t target,
                      uint32_t volume_start, uint32_t volume_blocks,
                      SSScsiRead read, SSScsiWrite write,
                      SSScsiCapacity capacity) {
    uint8_t result[8] __attribute__((aligned(4)));
    if (!device || target > 7 || !read || !write || !capacity ||
        capacity(target, result) != 0) return -1;
    uint32_t last = be32(result), size = be32(result + 4);
    if (last > 0x7fffffffu || (size != 512 && size != 1024)) return -1;
    uint32_t count = last + 1u;
    if (volume_start >= count) return -1;
    if (!volume_blocks) volume_blocks = count - volume_start;
    if (volume_blocks > count - volume_start) return -1;
    device->read = read;
    device->write = write;
    device->device_blocks = count;
    device->volume_start = volume_start;
    device->volume_blocks = volume_blocks;
    device->block_size = (uint16_t)size;
    device->target = target;
    return 0;
}

static int validate(SSScsiDevice* device, uint32_t sector, const void* buffer,
                    int* lba, int* block_code) {
    if (!device || !buffer || ((uintptr_t)buffer & 1u) || device->target > 7 ||
        (device->block_size != 512 && device->block_size != 1024) ||
        device->volume_start > device->device_blocks ||
        device->volume_blocks > device->device_blocks - device->volume_start ||
        sector >= device->volume_blocks ||
        device->volume_start + sector > 0x7fffffffu)
        return -1;
    *lba = (int)(device->volume_start + sector);
    *block_code = device->block_size == 512 ? 1 : 2;
    return 0;
}

int ss_scsi_read_sector(void* context, uint32_t sector, uint8_t* out) {
    SSScsiDevice* device = context;
    int lba, code;
    if (validate(device, sector, out, &lba, &code) || !device->read) return -1;
    return device->read(lba, 1, device->target, code, out) == 0 ? 0 : -1;
}

int ss_scsi_write_sector(void* context, uint32_t sector, const uint8_t* in) {
    SSScsiDevice* device = context;
    int lba, code;
    if (validate(device, sector, in, &lba, &code) || !device->write) return -1;
    return device->write(lba, 1, device->target, code, in) == 0 ? 0 : -1;
}
