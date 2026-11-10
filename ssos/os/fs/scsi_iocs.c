#include "scsi_volume.h"

#include <x68k/iocs.h>

static int capacity(int target, uint8_t* out) {
    struct iocs_readcap result;
    if (_iocs_s_readcap(target, &result) != 0) return -1;
    uint32_t fields[2] = {(uint32_t)result.block, (uint32_t)result.size};
    for (unsigned n = 0; n < 2; n++)
        for (unsigned b = 0; b < 4; b++)
            out[n * 4 + b] = (uint8_t)(fields[n] >> (24u - b * 8u));
    return 0;
}

int ss_scsi_configure_iocs(SSScsiDevice* device, uint8_t target,
                           uint32_t volume_start, uint32_t volume_blocks) {
    return ss_scsi_configure(device, target, volume_start, volume_blocks,
                             _iocs_s_readext, _iocs_s_writeext, capacity);
}

int ss_scsi_volume_mount_iocs(SSScsiVolume* volume, uint8_t target,
                              uint32_t start, uint32_t blocks) {
    if (!volume || volume->storage.mounted) return -1;
    SSScsiDevice device;
    if (ss_scsi_configure_iocs(&device, target, start, blocks)) return -1;
    return ss_scsi_volume_attach(volume, &device);
}
