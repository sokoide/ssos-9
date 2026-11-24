/* Human68k-format SCSI disk fixture shared by host and QEMU tests.
 *
 * The production mount path (ss_scsi_configure + ss_scsi_volume_attach)
 * accepts only a Human68k partition with at least 4085 clusters, so a test
 * volume is at least about 4 MiB. A test materializes just the sectors it
 * can touch: the partition table, the BPB, both FATs, the root directory,
 * and the first data clusters. Reading a sector past the store returns
 * zeros; writing one fails, so a test that outgrows its store fences the
 * volume instead of corrupting memory.
 *
 * The layout mirrors the XEiJ A: volume: partition entry "Human68k",
 * 1024-byte sectors, one sector per cluster, two FATs of 18 sectors (FAT2
 * is reserved and unused), 512 root entries, and a TEST directory on
 * cluster 2. FAT entries are big-endian, as on captured Human68k media.
 *
 * Usage:
 *   static uint8_t store[HD_STORE_BYTES(256)];
 *   hd_format(store, 256, 0);
 *   hd_mount(&volume, 3, 0, 0);          // the production attach path
 *   ss_storage_store(&volume.storage, ...);
 *
 * The fixture is header-only and freestanding so QEMU tests can use it. */

#ifndef SSOS9_TESTS_HUMAN68K_DISK_H
#define SSOS9_TESTS_HUMAN68K_DISK_H

#include "../../ssos/os/fs/scsi_volume.h"

#include <stdint.h>

#define HD_SECTOR 1024u
#define HD_PARTITION_START 32u
#define HD_FAT_COUNT 2u
#define HD_FAT_SECTORS 18u
#define HD_ROOT_ENTRIES 512u
#define HD_PARTITION_SECTORS 9000u
/* Read capacity reports this many 512-byte blocks (last LBA 19999). */
#define HD_DISK_BLOCKS 20000u
#define HD_BLOCK_BYTES 512u

#define HD_FAT_START (HD_PARTITION_START + 1u)                          /* 33 */
#define HD_ROOT_START (HD_FAT_START + HD_FAT_COUNT * HD_FAT_SECTORS)    /* 69 */
#define HD_ROOT_SECTORS (HD_ROOT_ENTRIES * 32u / HD_SECTOR)             /* 16 */
#define HD_DATA_START (HD_ROOT_START + HD_ROOT_SECTORS)                 /* 85 */
#define HD_CLUSTERS (HD_PARTITION_SECTORS - (HD_DATA_START - HD_PARTITION_START))
#define HD_FAT2_START (HD_FAT_START + HD_FAT_SECTORS)                   /* 51 */

/* Smallest store that holds the formatted volume and the TEST directory. */
#define HD_STORE_MIN (HD_DATA_START + 1u)

/* HD_ offsets count 1024-byte sectors from the start of the disk. The
 * SSFat12 of a mounted volume counts from the partition, 32 lower. */

/* Bytes of store for `sectors` materialized 1024-byte sectors. */
#define HD_STORE_BYTES(sectors) ((sectors) * HD_SECTOR)

typedef struct {
    uint8_t* bytes;         /* materialized sectors, `sectors` of them */
    uint32_t sectors;
    uint32_t window;        /* store sector 0, in 1024-byte disk sectors */
    int32_t fail_block;     /* 512-byte block whose access fails; -1 = none */
    int fail_reads;         /* fail_block fails reads as well as writes */
    int write_before_error; /* a failing write still stores its bytes */
    unsigned reads, writes;
} Human68kDisk;

static Human68kDisk hd_disk;

static inline void hd_put16(uint8_t* p, uint32_t n) {
    p[0] = (uint8_t)(n >> 8);
    p[1] = (uint8_t)n;
}

static inline void hd_put32(uint8_t* p, uint32_t n) {
    p[0] = (uint8_t)(n >> 24);
    p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
}

static inline void hd_text(uint8_t* p, const char* text, unsigned count) {
    for (unsigned i = 0; i < count; i++) p[i] = (uint8_t)text[i];
}

/* Logical sector of the mounted window; must be inside the store. */
static inline uint8_t* hd_logical(uint32_t sector) {
    return hd_disk.bytes + sector * HD_SECTOR;
}

static inline uint8_t* hd_fat(void) { return hd_logical(HD_FAT_START); }

/* Sector holding the first byte of FAT1's entry for `cluster`. */
static inline uint8_t* hd_fat_entry(uint32_t cluster) {
    uint32_t offset = cluster * 2u;
    return hd_logical(HD_FAT_START + offset / HD_SECTOR) + offset % HD_SECTOR;
}

static inline uint8_t* hd_cluster(uint32_t cluster) {
    return hd_logical(HD_DATA_START + cluster - 2u);
}

/* Partition table, BPB, FAT1 media bytes, and the TEST directory (cluster 2). */
static inline void hd_format(uint8_t* bytes, uint32_t sectors, uint32_t window) {
    hd_disk.bytes = bytes;
    hd_disk.sectors = sectors;
    hd_disk.window = window;
    hd_disk.fail_block = -1;
    hd_disk.fail_reads = 0;
    hd_disk.write_before_error = 0;
    hd_disk.reads = hd_disk.writes = 0;
    for (uint32_t i = 0; i < sectors * HD_SECTOR; i++) bytes[i] = 0;
    hd_text(hd_logical(0), "X68SCSI1", 8);
    hd_text(hd_logical(2), "X68K", 4);
    hd_text(hd_logical(2) + 16, "Human68k", 8);
    hd_put32(hd_logical(2) + 24, HD_PARTITION_START);
    hd_put32(hd_logical(2) + 28, HD_PARTITION_SECTORS);
    uint8_t* bpb = hd_logical(HD_PARTITION_START);
    hd_put16(bpb + 0x12, HD_SECTOR);
    bpb[0x14] = 1;                    /* one sector per cluster */
    bpb[0x15] = HD_FAT_COUNT;         /* big-endian FAT entries */
    hd_put16(bpb + 0x16, 1);          /* reserved sectors */
    hd_put16(bpb + 0x18, HD_ROOT_ENTRIES);
    bpb[0x1c] = 0xf7;                 /* media descriptor */
    bpb[0x1d] = HD_FAT_SECTORS;
    hd_put32(bpb + 0x1e, HD_PARTITION_SECTORS);
    hd_put32(bpb + 0x22, HD_PARTITION_START);
    uint8_t* fat = hd_fat();
    fat[0] = 0xf7;
    fat[1] = fat[2] = fat[3] = 0xff;
    fat[4] = fat[5] = 0xff;           /* cluster 2: the TEST directory */
    uint8_t* root = hd_logical(HD_ROOT_START);
    hd_text(root, "TEST       ", 11);
    root[11] = 0x10;
    root[26] = 2;
    uint8_t* dir = hd_cluster(2);
    hd_text(dir, ".          ", 11);
    dir[11] = 0x10;
    dir[26] = 2;
    hd_text(dir + 32, "..         ", 11);
    dir[43] = 0x10;
}

/* SSScsiRead/SSScsiWrite/SSScsiCapacity over the fixture. LBAs are
 * 512-byte blocks of the whole disk; the mounted window starts at
 * `window` sectors. */
static inline int hd_read(int lba, int count, int target, int code, void* out) {
    (void)target;
    hd_disk.reads++;
    if (count != 1 || code != 1 || lba < 0 || lba >= (int)HD_DISK_BLOCKS) return -1;
    uint32_t block = (uint32_t)lba;
    uint32_t index = block / (HD_SECTOR / HD_BLOCK_BYTES);
    if (index < hd_disk.window) return -1; /* before the mounted window */
    index -= hd_disk.window;
    if (hd_disk.fail_block >= 0 && hd_disk.fail_reads &&
        (uint32_t)hd_disk.fail_block == block)
        return -1;
    const uint8_t* src = 0;
    if (index < hd_disk.sectors)
        src = hd_logical(index) +
              (block % (HD_SECTOR / HD_BLOCK_BYTES)) * HD_BLOCK_BYTES;
    for (unsigned i = 0; i < HD_BLOCK_BYTES; i++)
        ((uint8_t*)out)[i] = src ? src[i] : 0;
    return 0;
}

static inline int hd_write(int lba, int count, int target, int code,
                           const void* in) {
    (void)target;
    hd_disk.writes++;
    if (count != 1 || code != 1 || lba < 0 || lba >= (int)HD_DISK_BLOCKS) return -1;
    uint32_t block = (uint32_t)lba;
    uint32_t index = block / (HD_SECTOR / HD_BLOCK_BYTES);
    if (index < hd_disk.window) return -1; /* before the mounted window */
    index -= hd_disk.window;
    int failing = hd_disk.fail_block >= 0 && (uint32_t)hd_disk.fail_block == block;
    if (failing && !hd_disk.write_before_error) return -1;
    if (index >= hd_disk.sectors) return -1;
    uint8_t* dst = hd_logical(index) +
                   (block % (HD_SECTOR / HD_BLOCK_BYTES)) * HD_BLOCK_BYTES;
    for (unsigned i = 0; i < HD_BLOCK_BYTES; i++) dst[i] = ((const uint8_t*)in)[i];
    return failing ? -1 : 0;
}

static inline int hd_capacity(int target, uint8_t* out) {
    (void)target;
    hd_put32(out, HD_DISK_BLOCKS - 1u);
    hd_put32(out + 4, HD_BLOCK_BYTES);
    return 0;
}

/* The production mount path with the fixture callbacks: configure the
 * device window, then probe and attach. */
static inline int hd_mount(SSScsiVolume* volume, uint8_t target,
                           uint32_t start, uint32_t blocks) {
    SSScsiDevice device;
    if (ss_scsi_configure(&device, target, start, blocks, hd_read, hd_write,
                          hd_capacity) != 0)
        return -1;
    return ss_scsi_volume_attach(volume, &device);
}

#endif
