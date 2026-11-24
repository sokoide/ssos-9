#ifndef SS_FAT12_H
#define SS_FAT12_H

#include <stdint.h>

/* FAT reader and writer. The reader parses a generic BPB (512- or 1024-byte
 * sectors, FAT12 or FAT16) for floppy images; the writer accepts only a
 * Human68k-format FAT16 volume. The block reader reads one logical sector
 * and returns 0 on success. Existing names are retained for callers. */
typedef int (*SSFat12ReadSector)(void* context, uint32_t sector, uint8_t* out);
typedef int (*SSFatWriteSector)(void* context, uint32_t sector, const uint8_t* in);

#define SS_FAT_SECTOR_BYTES 1024u

typedef struct {
    SSFat12ReadSector read_sector;
    void* context;
    uint32_t total_sectors;
    uint32_t fat_start;
    uint32_t root_start;
    uint32_t data_start;
    uint16_t fat_sectors;
    uint16_t root_entries;
    uint16_t cluster_count;
    uint16_t sector_size;
    uint8_t sectors_per_cluster;
    uint8_t fat_bits; /* 12 or 16; determined by the BPB cluster count. */
    uint8_t fat_count;
    uint8_t human68k;
    uint8_t fat_big_endian;
} SSFat12;

typedef struct {
    SSFat12* volume;
    uint32_t size;
    uint32_t offset;
    uint16_t cursor_cluster;
    uint16_t cursor_index;
} SSFat12File;

int ss_fat12_mount(SSFat12* volume, SSFat12ReadSector read_sector,
                   void* context, uint32_t available_sectors);
int ss_fat12_open(SSFat12* volume, const char* name, SSFat12File* file);
int32_t ss_fat12_read(void* context, uint8_t* out, uint32_t size);

/* Sector scratch buffers shared by every FAT routine. Callers serialize all
 * access to a volume (storage.h), so two buffers are enough: FAT sectors use
 * one, directory and file data sectors the other. A caller that keeps a data
 * sector live only nests FAT access inside it, never another data read.
 * They replace a 1 KiB stack frame in each routine, so kernel stack use no
 * longer depends on the sector size. */
uint8_t* ss_fat_fat_scratch(void);
uint8_t* ss_fat_data_scratch(void);

/* Resolve an ASCII 8.3 path; location denotes a matching or free entry. */
typedef struct {
    uint32_t sector;
    uint16_t offset;
    uint8_t found;
    uint8_t was_end;
    uint8_t entry[32];
    uint8_t name[11];
    uint32_t next_sector;
    uint8_t has_next_sector;
} SSFatLocation;
int ss_fat_locate(SSFat12* volume, const char* path, SSFatLocation* location);

/* Create a new root-directory 8.3 file, or replace an existing regular file
 * with a newly allocated chain. The old chain is released only after the new
 * directory entry is written. The caller serializes all access to the
 * volume. A failed metadata write may leave allocated clusters: the device
 * has no transactional write guarantee and must be checked before reuse. */
int ss_fat16_create(SSFat12* volume, SSFatWriteSector write_sector,
                    const char* name, const uint8_t* data, uint32_t size);
int ss_fat16_replace(SSFat12* volume, SSFatWriteSector write_sector,
                     const char* name, const uint8_t* data, uint32_t size);

/* Private, unpublished replacement chain. Access must be serialized and the
 * volume must not be changed by other writers until commit or abort. */
typedef struct {
    uint32_t slot;
    uint32_t size;
    uint16_t first;
    uint16_t last;
    uint16_t old_cluster;
    uint32_t human_sector;
    uint16_t human_offset;
    uint32_t human_next_sector;
    uint8_t human_has_next_sector;
    uint8_t name[11];
    uint8_t slot_was_end;
    uint8_t replacing;
    uint8_t active;
} SSFat16Writer;

int ss_fat16_writer_begin(SSFat12* volume, const char* name, int replace,
                          SSFat16Writer* writer);
int32_t ss_fat16_writer_write(SSFat12* volume, SSFatWriteSector write_sector,
                              SSFat16Writer* writer, const uint8_t* data,
                              uint32_t size);
int ss_fat16_writer_commit(SSFat12* volume, SSFatWriteSector write_sector,
                           SSFat16Writer* writer);
int ss_fat16_writer_abort(SSFat12* volume, SSFatWriteSector write_sector,
                          SSFat16Writer* writer);

#endif
