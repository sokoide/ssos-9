/* On-target assemble/link/run pipeline: store the linker sample sources
 * on FAT16, run the real S9M1 asm module four times, link the objects
 * with the real ld module, then execute the linked PROG image and check
 * its exit status and console output against the host-built golden. */

#include "../../../ssos/os/app/cli.h"
#include "../../../ssos/os/app/cli_console.h"
#include "../../../ssos/os/app/cli_wait.h"
#include "../../../ssos/os/fs/scsi_volume.h"
#include "builtin.h"
#include "main_task.h"
#include "process.h"
#include "ssos9/syscall.h"
#include "syscall.h"
#include "system_arena.h"
#include "tty.h"

static SSTask main_tcb;
/* 96 sectors: the sample files plus their objects fill more of the FAT16
 * volume than the 50-sector fixture t13 uses. */
#define T14_DISK_SECTORS 96
static uint8_t scsi_test_disk[T14_DISK_SECTORS][512];
extern const uint8_t ss_test_asm_image[], ss_test_asm_image_end[];
extern const uint8_t ss_test_ld_image[], ss_test_ld_image_end[];
extern const uint8_t ss_test_link_start_src[], ss_test_link_start_src_end[];
extern const uint8_t ss_test_link_main_src[], ss_test_link_main_src_end[];
extern const uint8_t ss_test_link_obj_src[], ss_test_link_obj_src_end[];
extern const uint8_t ss_test_link_badmain_src[], ss_test_link_badmain_src_end[];
extern const uint8_t ss_test_link_expected[], ss_test_link_expected_end[];
extern uint8_t ss_test_console_bytes[256];
extern uint32_t ss_test_console_count;
extern void ss_test_install_syscall_vector(void);
extern void ss_test_install_ctrl_c_vector(void);
extern int32_t ss_test_syscall_roundtrip(void);

static int scsi_test_read(void* context, uint32_t sector, uint8_t* out) {
    (void)context;
    if (sector >= T14_DISK_SECTORS)
        return -1;
    for (unsigned i = 0; i < 512; i++) out[i] = scsi_test_disk[sector][i];
    return 0;
}
static int scsi_test_write(void* context, uint32_t sector, const uint8_t* in) {
    (void)context;
    if (sector >= T14_DISK_SECTORS)
        return -1;
    for (unsigned i = 0; i < 512; i++) scsi_test_disk[sector][i] = in[i];
    return 0;
}

int ss_scsi_volume_mount_iocs(SSScsiVolume* volume, uint8_t target,
                              uint32_t start, uint32_t blocks) {
    (void)blocks;
    if (target != 3 || start != 10)
        return -1;
    return ss_storage_mount(&volume->storage, scsi_test_read, scsi_test_write,
                            NULL, 5000, 512);
}

int _iocs_b_read(int mode, int position, int size, void* out) {
    (void)mode;
    (void)position;
    (void)size;
    (void)out;
    return -1;
}

int _iocs_b_keysns(void) { return 0; }

int _iocs_b_keyinp(void) { return 0; }

static void fail(const char* reason) {
    tty_puts("FAIL ");
    tty_puts(reason);
    tty_puts("\n");
    for (;;) __asm__ volatile("trap #1" ::: "memory");
}

static void tick(void) { __asm__ volatile("trap #1" ::: "memory"); }

static int32_t call_sys(uint32_t number, uint32_t request) {
    register uint32_t d0 __asm__("d0") = number;
    register uint32_t d1 __asm__("d1") = request;
    __asm__ volatile("trap #0" : "+d"(d0), "+d"(d1) : : "cc", "memory");
    return (int32_t)d0;
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

static void store_file(const char* name, const uint8_t* data, uint32_t size) {
    SS9FileRequest request __attribute__((aligned(4))) = {
        (uint32_t)(uintptr_t)name, (uint32_t)(uintptr_t)data, size,
        SS9_FILE_CREATE};
    if (call_sys(SS9_SYS_FILE_STORE, (uint32_t)(uintptr_t)&request) !=
        (int32_t)size)
        fail("file store");
}

/* Spawns a registered module in the foreground and reaps it. Returns the
 * wait status; the exit status lands in *status_out. */
static int16_t spawn_and_wait(SSModuleId module, int argc,
                              const char* const* argv, uint16_t* status_out) {
    SSProcessId pid;
    if (ss_process_spawn(1, module, argc, argv, &pid) != SS_OK)
        fail("spawn module");
    ss_cli_set_foreground_pid(pid);
    int16_t wait = SS_ERR_STATE;
    for (unsigned i = 0; i < 20000 && wait == SS_ERR_STATE; i++) {
        tick();
        wait = ss_process_wait(1, pid, status_out);
    }
    ss_cli_set_foreground_pid(0);
    return wait;
}

/* Checks an .O produced by on-target asm exists and is non-empty. */
static void check_object(const char* name) {
    static uint8_t buffer[512];
    if (ss_system_file_load(name, buffer, sizeof(buffer)) <= 0)
        fail("object file missing");
}

static int image_magic_ok(const uint8_t* image) {
    return image[0] == 'S' && image[1] == '9' && image[2] == 'M' &&
           image[3] == '1';
}

int main(void) {
    tty_puts("START link-run\n");
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
    uint32_t free_before = ss_memory_free_bytes();

    scsi_test_disk[0][12] = 2;
    scsi_test_disk[0][13] = 1;
    scsi_test_disk[0][14] = 1;
    scsi_test_disk[0][16] = 2;
    scsi_test_disk[0][17] = 16;
    scsi_test_disk[0][19] = 0x88;
    scsi_test_disk[0][20] = 0x13;
    scsi_test_disk[0][22] = 20;
    scsi_test_disk[0][510] = 0x55;
    scsi_test_disk[0][511] = 0xaa;
    char mount_command[] = "smount 3 10";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(mount_command) != 0 ||
        !console_contains("FAT16 mounted."))
        fail("SCSI FAT16 mount");

    store_file("START.S", ss_test_link_start_src,
               (uint32_t)(ss_test_link_start_src_end - ss_test_link_start_src));
    store_file("MAIN.S", ss_test_link_main_src,
               (uint32_t)(ss_test_link_main_src_end - ss_test_link_main_src));
    store_file("OBJ.S", ss_test_link_obj_src,
               (uint32_t)(ss_test_link_obj_src_end - ss_test_link_obj_src));
    store_file(
        "BADMAIN.S", ss_test_link_badmain_src,
        (uint32_t)(ss_test_link_badmain_src_end - ss_test_link_badmain_src));

    /* On-target asm builds the three input objects. Expected section
     * sizes (host asm-host): START 10/0/0, MAIN 44/16/4, OBJ 6/8/0. */
    SSModuleId asm_module;
    if (ss_module_register_image(
            ss_test_asm_image,
            (uint32_t)(ss_test_asm_image_end - ss_test_asm_image),
            SS_MODULE_STORAGE_DYNAMIC, &asm_module) != SS_OK)
        fail("register assembler image");
    uint16_t status;
    const char* asm_start_argv[] = {"asm", "START.S", "START.O"};
    ss_test_console_count = 0;
    if (spawn_and_wait(asm_module, 3, asm_start_argv, &status) != SS_OK ||
        status != 0 || !console_contains("Wrote START.O") ||
        !console_contains("text 10, data 0, bss 0"))
        fail("asm START.O");
    check_object("START.O");
    const char* asm_main_argv[] = {"asm", "MAIN.S", "MAIN.O"};
    ss_test_console_count = 0;
    if (spawn_and_wait(asm_module, 3, asm_main_argv, &status) != SS_OK ||
        status != 0 || !console_contains("Wrote MAIN.O") ||
        !console_contains("text 44, data 16, bss 4"))
        fail("asm MAIN.O");
    check_object("MAIN.O");
    const char* asm_obj_argv[] = {"asm", "OBJ.S", "OBJ.O"};
    ss_test_console_count = 0;
    if (spawn_and_wait(asm_module, 3, asm_obj_argv, &status) != SS_OK ||
        status != 0 || !console_contains("Wrote OBJ.O") ||
        !console_contains("text 6, data 8, bss 0"))
        fail("asm OBJ.O");
    check_object("OBJ.O");

    /* On-target ld links PROG.S9M from the three objects. The host
     * golden (link-host over the same inputs) is 180 bytes. */
    SSModuleId ld_module;
    if (ss_module_register_image(
            ss_test_ld_image,
            (uint32_t)(ss_test_ld_image_end - ss_test_ld_image),
            SS_MODULE_STORAGE_DYNAMIC, &ld_module) != SS_OK)
        fail("register linker image");
    const char* ld_argv[] = {"ld", "PROG.S9M", "START.O", "MAIN.O", "OBJ.O"};
    ss_test_console_count = 0;
    if (spawn_and_wait(ld_module, 5, ld_argv, &status) != SS_OK ||
        status != 0 || !console_contains("Wrote PROG.S9M"))
        fail("ld PROG.S9M");

    uint32_t expected_size =
        (uint32_t)(ss_test_link_expected_end - ss_test_link_expected);
    static uint8_t prog_image[512];
    if (expected_size != 180 || expected_size > sizeof(prog_image))
        fail("golden link size");
    if (ss_system_file_load("PROG.S9M", prog_image, sizeof(prog_image)) !=
            (int32_t)expected_size ||
        !image_magic_ok(prog_image))
        fail("linked image size or magic");
    for (uint32_t i = 0; i < expected_size; i++)
        if (prog_image[i] != ss_test_link_expected[i])
            fail("linked image bytes");

    /* Execute the linked image: main returns base(7) + factor(10) +
     * helper's factor(10) = 27 and writes "LNK\n" through SYS_WRITE. */
    SSModuleId prog_module;
    if (ss_module_register_image(prog_image, expected_size,
                                 SS_MODULE_STORAGE_DYNAMIC,
                                 &prog_module) != SS_OK)
        fail("register linked image");
    ss_test_console_count = 0;
    if (spawn_and_wait(prog_module, 0, NULL, &status) != SS_OK ||
        status != 27 || !console_contains("LNK\n"))
        fail("run linked image");

    /* CLI FAT16 fallback: with PROG unregistered, `run PROG` must resolve
     * through the shell's load_disk_module path (registry miss -> FAT16
     * open of PROG/PROG.S9M -> DYNAMIC register -> foreground spawn). */
    if (ss_module_unload(prog_module) != SS_OK)
        fail("unload linked image");
    char run_prog_command[] = "run PROG";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(run_prog_command) != 0 ||
        !console_contains("LNK\n") ||
        !console_contains("exited with status 27\r\n"))
        fail("CLI run PROG fallback");
    char unload_prog_command[] = "unload PROG";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(unload_prog_command) != 0 ||
        !console_contains("Module unloaded.") ||
        ss_module_find("PROG", &prog_module) != SS_ERR_ID)
        fail("CLI unload PROG");

    /* Negative: BADMAIN.O references an undefined extern, so ld must
     * fail and leave no BAD.S9M output. */
    const char* asm_bad_argv[] = {"asm", "BADMAIN.S", "BADMAIN.O"};
    ss_test_console_count = 0;
    if (spawn_and_wait(asm_module, 3, asm_bad_argv, &status) != SS_OK ||
        status != 0 || !console_contains("Wrote BADMAIN.O"))
        fail("asm BADMAIN.O");
    const char* ld_bad_argv[] = {"ld", "BAD.S9M", "BADMAIN.O"};
    ss_test_console_count = 0;
    if (spawn_and_wait(ld_module, 3, ld_bad_argv, &status) != SS_OK ||
        status == 0 || !console_contains("Undefined symbol missing"))
        fail("ld undefined symbol");
    static uint8_t absent[64];
    if (ss_system_file_load("BAD.S9M", absent, sizeof(absent)) >= 0)
        fail("failed ld wrote output");

    if (ss_module_unload(ld_module) != SS_OK ||
        ss_module_unload(asm_module) != SS_OK)
        fail("module unload");
    if (ss_memory_free_bytes() != free_before)
        fail("arena baseline after unload");

    tty_puts("OK on-target assemble/link/run\n");
    for (;;) tick();
}
