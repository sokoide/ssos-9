/* CLI smount through the production SCSI IOCS mount path. Only the
 * low-level IOCS calls (_iocs_s_readcap/readext/writeext) are mocked;
 * ss_scsi_configure_iocs, ss_scsi_volume_attach, and the storage layer run
 * unmodified. A probe rejection case serves an MS-DOS partition table on a
 * second target. Unlike t13/t15, ss_scsi_volume_mount_iocs is not
 * replaced, so `smount` exercises probe -> attach -> storage end to end. */

#include "../../../ssos/os/app/cli.h"
#include "../../../ssos/os/app/cli_console.h"
#include "builtin.h"
#include "main_task.h"
#include "process.h"
#include "system_arena.h"
#include "tty.h"

#include <stdint.h>

/* The QEMU iocs.h stub does not carry the SCSI readcap layout, so declare
 * the toolchain's struct at file scope (two unsigned longs on m68k-elf);
 * scsi_iocs.o is compiled against the real toolchain header and only the
 * layout must agree across the link. */
struct iocs_readcap {
    unsigned long block;
    unsigned long size;
};

static SSTask main_tcb;
/* 1024-byte storage sectors; only the sectors the fixture and the FAT16
 * traffic touch are materialized. */
#define T17_STORAGE_SECTORS 128
static uint8_t fat_blocks[T17_STORAGE_SECTORS][1024];
static uint8_t dos_blocks[8][1024];

extern uint8_t ss_test_console_bytes[256];
extern uint32_t ss_test_console_count;
extern void ss_test_install_syscall_vector(void);
extern void ss_test_install_ctrl_c_vector(void);
extern int32_t ss_test_syscall_roundtrip(void);

static void put16(uint8_t* p, uint16_t n) { p[0] = n >> 8; p[1] = n; }
static void put32(uint8_t* p, uint32_t n) {
    p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n;
}

static void build_fat_fixture(void) {
    const char* id = "X68SCSI1";
    for (unsigned i = 0; i < 8; i++) fat_blocks[0][i] = id[i];
    const char* x68k = "X68K";
    const char* name = "Human68k";
    for (unsigned i = 0; i < 4; i++) fat_blocks[2][i] = x68k[i];
    for (unsigned i = 0; i < 8; i++) fat_blocks[2][16 + i] = name[i];
    put32(fat_blocks[2] + 24, 32);
    put32(fat_blocks[2] + 28, 9000);
    put16(fat_blocks[32] + 0x12, 1024);
    fat_blocks[32][0x14] = 1;
    fat_blocks[32][0x15] = 2;
    put16(fat_blocks[32] + 0x16, 1);
    put16(fat_blocks[32] + 0x18, 512);
    fat_blocks[32][0x1c] = 0xf7;
    fat_blocks[32][0x1d] = 18;
    put32(fat_blocks[32] + 0x1e, 9000);
    put32(fat_blocks[32] + 0x22, 32);
    /* FAT1 media descriptor and EOC entries; FAT2 stays unused. */
    uint8_t* fat = fat_blocks[33];
    fat[0] = 0xf7;
    fat[1] = fat[2] = fat[3] = fat[4] = fat[5] = 0xff;
    const char* dir = "TEST       ";
    for (unsigned i = 0; i < 11; i++) fat_blocks[69][i] = dir[i];
    fat_blocks[69][11] = 0x10;
    fat_blocks[69][26] = 2;
}

static void build_dos_fixture(void) {
    const char* id = "X68SCSI1";
    for (unsigned i = 0; i < 8; i++) dos_blocks[0][i] = id[i];
    const char* x68k = "X68K";
    const char* name = "MS-DOS  ";
    for (unsigned i = 0; i < 4; i++) dos_blocks[2][i] = x68k[i];
    for (unsigned i = 0; i < 8; i++) dos_blocks[2][16 + i] = name[i];
    put32(dos_blocks[2] + 24, 32);
    put32(dos_blocks[2] + 28, 9000);
}

/* The mock stops at the IOCS boundary: readcap reports the volume size and
 * 512-byte blocks, readext/writeext move half-kilobyte SCSI blocks. LBA l
 * maps to storage sector l/2, high word first, exactly as t16's device
 * mock does for ss_scsi_volume_attach. */
static int t17_readcap(int target, struct iocs_readcap* buf) {
    if (target == 3) {
        buf->block = 19999;
        buf->size = 512;
        return 0;
    }
    if (target == 4) {
        buf->block = 255;
        buf->size = 512;
        return 0;
    }
    return -1;
}

static int t17_readext(int lba, int count, int target, int code, void* out) {
    if (count != 1 || code != 1)
        return -1;
    const uint8_t* src = 0;
    if (target == 3) {
        if (lba < 0 || lba >= 2 * T17_STORAGE_SECTORS)
            return -1;
        src = fat_blocks[lba / 2] + (unsigned)(lba % 2) * 512;
    } else if (target == 4) {
        if (lba < 0 || lba >= 8)
            return -1;
        src = dos_blocks[lba / 2] + (unsigned)(lba % 2) * 512;
    } else
        return -1;
    for (unsigned i = 0; i < 512; i++) ((uint8_t*)out)[i] = src[i];
    return 0;
}

static int t17_writeext(int lba, int count, int target, int code,
                        const void* in) {
    if (count != 1 || code != 1 || target != 3 || lba < 0 ||
        lba >= 2 * T17_STORAGE_SECTORS)
        return -1;
    uint8_t* dst = fat_blocks[lba / 2] + (unsigned)(lba % 2) * 512;
    for (unsigned i = 0; i < 512; i++) dst[i] = ((const uint8_t*)in)[i];
    return 0;
}

int _iocs_s_readcap(int target, struct iocs_readcap* buf) {
    return t17_readcap(target, buf);
}
int _iocs_s_readext(int lba, int count, int target, int code, void* out) {
    return t17_readext(lba, count, target, code, out);
}
int _iocs_s_writeext(int lba, int count, int target, int code,
                     const void* in) {
    return t17_writeext(lba, count, target, code, in);
}

int _iocs_b_keysns(void) { return 0; }

int _iocs_b_keyinp(void) { return 0; }

static void fail(const char* reason) {
    tty_puts("FAIL ");
    tty_puts(reason);
    tty_puts("\n");
    for (;;) __asm__ volatile("trap #1" ::: "memory");
}

static int console_contains(const char* needle) {
    for (uint32_t i = 0; i < ss_test_console_count; i++) {
        uint32_t j = 0;
        while (needle[j] && i + j < ss_test_console_count &&
               ss_test_console_bytes[i + j] == (uint8_t)needle[j])
            j++;
        if (!needle[j])
            return 1;
    }
    return 0;
}

int main(void) {
    tty_puts("START human68k-cli-mount\n");
    ss_test_install_syscall_vector();
    ss_test_install_ctrl_c_vector();
    if (ss_test_syscall_roundtrip() != 0)
        fail("syscall register preservation");
    if (ss_memory_init() != SS_OK)
        fail("memory init");
    ss_module_registry_init();
    if (ss_builtin_modules_init() != SS_OK)
        fail("built-in modules");
    ss_sched_init();
    if (ss_main_task_register(&main_tcb, 8) != SS_OK ||
        ss_idle_task_start() != SS_OK || ss_reaper_task_start() != SS_OK ||
        ss_process_init(&main_tcb) != SS_OK)
        fail("kernel tables init");
    build_fat_fixture();
    build_dos_fixture();
    uint32_t free_before = ss_memory_free_bytes();

    /* Probe rejection first: target 4 serves an MS-DOS partition entry, so
     * the production mount must refuse before the volume stays unmounted
     * for the real FAT16 mount below. */
    char rejected_mount[] = "smount 4";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(rejected_mount) != 0 ||
        !console_contains("Unable to mount FAT16."))
        fail("smount probe rejection");

    char mount_command[] = "smount 3";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(mount_command) != 0 ||
        !console_contains("FAT16 mounted."))
        fail("smount production mount");

    /* The fixture's hand-built TEST directory entry proves the production
     * probe/attach consumed it; the write lands inside the mocked blocks
     * and sread returns the known bytes through the CLI. */
    char write_command[] = "swrite TEST/A.TXT t17data";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(write_command) != 0 ||
        !console_contains("File written."))
        fail("smount FAT16 write");

    char read_command[] = "sread TEST/A.TXT";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(read_command) != 0 ||
        !console_contains("t17data"))
        fail("smount FAT16 read");

    char missing_read[] = "sread TEST/MISSING.TXT";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(missing_read) != 0 ||
        !console_contains("Unable to read file (maximum 256 bytes)."))
        fail("smount FAT16 missing read");

    if (ss_memory_free_bytes() != free_before)
        fail("arena baseline");

    tty_puts("OK CLI smount Human68k FAT16 mount write read\n");
    for (;;) {}
}
