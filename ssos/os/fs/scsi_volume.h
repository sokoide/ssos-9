#ifndef SS_SCSI_VOLUME_H
#define SS_SCSI_VOLUME_H

#include "scsi.h"
#include "storage.h"

typedef struct {
    SSScsiDevice device;
    SSStorage storage;
} SSScsiVolume;
extern SSScsiVolume ss_system_scsi_volume;

/* Initialize the structure to zero. The caller serializes IOCS and all volume
 * access; device and volume are copied only after successful validation. */
int ss_scsi_volume_attach(SSScsiVolume* volume, const SSScsiDevice* device);
int ss_scsi_volume_mount_iocs(SSScsiVolume* volume, uint8_t target,
                              uint32_t start, uint32_t blocks);

#endif
