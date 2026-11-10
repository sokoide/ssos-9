#ifndef SS_SCSI_H
#define SS_SCSI_H

#include <stdint.h>

/* IOCS _S_READEXT/_S_WRITEEXT signatures. The caller owns IOCS locking and
 * provides the validated physical device size and volume start in blocks. */
typedef int (*SSScsiRead)(int, int, int, int, void*);
typedef int (*SSScsiWrite)(int, int, int, int, const void*);
/* Returns the eight raw READ CAPACITY bytes in big-endian order. */
typedef int (*SSScsiCapacity)(int, uint8_t*);
typedef struct {
    SSScsiRead read;
    SSScsiWrite write;
    uint32_t device_blocks;
    uint32_t volume_start;
    uint32_t volume_blocks;
    uint16_t block_size;
    uint8_t target;
} SSScsiDevice;

int ss_scsi_read_sector(void* context, uint32_t sector, uint8_t* out);
int ss_scsi_write_sector(void* context, uint32_t sector, const uint8_t* in);
int ss_scsi_configure(SSScsiDevice* device, uint8_t target,
                      uint32_t volume_start, uint32_t volume_blocks,
                      SSScsiRead read, SSScsiWrite write,
                      SSScsiCapacity capacity);
/* Must be called with IOCS access serialized. A zero volume_blocks uses the
 * device remainder beginning at volume_start. */
int ss_scsi_configure_iocs(SSScsiDevice* device, uint8_t target,
                           uint32_t volume_start, uint32_t volume_blocks);

#endif
