#ifndef SS_HUMAN68K_VOLUME_H
#define SS_HUMAN68K_VOLUME_H

#include <stdint.h>

typedef int (*SSHuman68kReadSector)(void*, uint32_t, uint8_t*);
typedef struct {
    uint32_t start, sectors, fat_start, root_start, data_start, clusters;
    uint16_t sector_size, root_entries;
    uint8_t sectors_per_cluster, fat_sectors, fat_count, fat_little_endian;
} SSHuman68kVolume;

/* Sector numbers and disk_sectors are in 1024-byte units. No writes. */
int ss_human68k_probe(SSHuman68kVolume* out, SSHuman68kReadSector read,
                      void* context, uint32_t disk_sectors, uint8_t partition);

#endif
