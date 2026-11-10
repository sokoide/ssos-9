#include "scsi_volume.h"

int ss_scsi_volume_attach(SSScsiVolume* volume, const SSScsiDevice* device) {
    if (!volume || !device || volume->storage.mounted || !device->read ||
        !device->write) return -1;
    SSScsiDevice candidate = *device;
    SSStorage storage = {0};
    if (ss_storage_mount(&storage, ss_scsi_read_sector, ss_scsi_write_sector,
                         &candidate, candidate.volume_blocks,
                         candidate.block_size)) return -1;
    volume->device = candidate;
    volume->storage = storage;
    volume->storage.volume.context = &volume->device;
    return 0;
}
