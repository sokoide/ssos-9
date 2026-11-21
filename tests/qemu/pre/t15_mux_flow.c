/* Mux three-console flow: create START.S with the real editor on screen
 * 1 (mux window 0), assemble and link it on screen 2 with the real asm
 * and ld modules, then execute the linked PROG image on screen 3 through
 * the shell's FAT16 fallback. Covers the Phase 4 completion item: edit
 * on one screen, assemble/link on another, run on a third, over FAT16.
 * Screen 2 then runs the mux shell lifecycle: background child + wait,
 * shell exit, sibling windows keep answering, and the root `mux` reaps
 * the zombie and respawns the shell. */

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
/* 96 sectors: sources, objects, and the linked image, as in t14. */
#define T15_DISK_SECTORS 96
static uint8_t scsi_test_disk[T15_DISK_SECTORS][512];
extern const uint8_t ss_test_edit_image[], ss_test_edit_image_end[];
extern const uint8_t ss_test_asm_image[], ss_test_asm_image_end[];
extern const uint8_t ss_test_ld_image[], ss_test_ld_image_end[];
extern const uint8_t ss_test_link_start_src[], ss_test_link_start_src_end[];
extern const uint8_t ss_test_link_main_src[], ss_test_link_main_src_end[];
extern const uint8_t ss_test_link_obj_src[], ss_test_link_obj_src_end[];
extern const uint8_t ss_test_link_expected[], ss_test_link_expected_end[];
extern uint8_t ss_test_console_bytes[256];
extern uint32_t ss_test_console_count;
extern uint32_t ss_test_console_locate_rows[32];
extern void ss_test_install_syscall_vector(void);
extern void ss_test_install_ctrl_c_vector(void);
extern int32_t ss_test_syscall_roundtrip(void);

/* The mux queue holds 64 keys per window and the longest typed line is
 * "run ld PROG.S9M START.O MAIN.O OBJ.O\r" (38 events), so the fake
 * keyboard queue is deeper than t13's eight-key fixture. Entries are
 * 16-bit so scan<<8 events (cursor keys, DEL) drive the real key path. */
static uint16_t fake_keys[128];
static uint8_t fake_key_head;
static uint8_t fake_key_count;

static int scsi_test_read(void* context, uint32_t sector, uint8_t* out) {
    (void)context;
    if (sector >= T15_DISK_SECTORS)
        return -1;
    for (unsigned i = 0; i < 512; i++) out[i] = scsi_test_disk[sector][i];
    return 0;
}
static int scsi_test_write(void* context, uint32_t sector, const uint8_t* in) {
    (void)context;
    if (sector >= T15_DISK_SECTORS)
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

int _iocs_b_keysns(void) { return fake_key_count; }

int _iocs_b_keyinp(void) {
    if (fake_key_count == 0)
        return 0;
    uint16_t key = fake_keys[fake_key_head++ %
                             (sizeof(fake_keys) / sizeof(fake_keys[0]))];
    fake_key_count--;
    return key;
}

static void push_key(uint16_t key) {
    uint16_t tail =
        (uint16_t)((fake_key_head + fake_key_count) %
                   (sizeof(fake_keys) / sizeof(fake_keys[0])));
    fake_keys[tail] = key;
    fake_key_count++;
}

static void fail(const char* reason) {
    tty_puts("FAIL ");
    tty_puts(reason);
    tty_puts("\n");
    for (;;) __asm__ volatile("trap #1" ::: "memory");
}

static void tick(void) { __asm__ volatile("trap #1" ::: "memory"); }

static void run_ticks(unsigned count) {
    for (unsigned i = 0; i < count; i++) tick();
}

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

/* Runs the scheduler until the active screen's shell output contains the
 * needle, then lets the tail of the output drain. */
static void wait_console(const char* needle) {
    for (unsigned i = 0; i < 20000; i++) {
        if (console_contains(needle)) {
            run_ticks(40);
            return;
        }
        tick();
    }
    fail("console wait");
}

/* Types "~<digit>" and waits for the multiplexer to select the window. */
static void switch_screen(uint8_t window) {
    push_key('~');
    push_key((uint8_t)('1' + window));
    for (unsigned i = 0; i < 400; i++) {
        if (ss_cli_console_mux_active() == window) {
            run_ticks(40);
            return;
        }
        tick();
    }
    fail("mux switch");
}

/* Types a shell command line and presses return. */
static void type_line(const char* line) {
    while (*line) push_key((uint8_t)*line++);
    push_key('\r');
}

/* Types "wait <pid>" so the mux shell takes the interactive
 * ss_cli_wait_pid path instead of the subshell polling branch. */
static void type_wait_line(SSProcessId pid) {
    char digits[12];
    unsigned count = 0;
    uint32_t value = pid;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    char line[24] = "wait ";
    unsigned used = 5;
    while (count != 0) line[used++] = digits[--count];
    line[used] = '\0';
    type_line(line);
}

/* Feeds editor text in chunks: the editor consumes roughly one key per
 * two ticks (render plus sleep), and each mux window queue holds 64. */
static void type_editor_text(const uint8_t* text, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) {
        push_key(text[i]);
        if ((i & 31u) == 31u)
            run_ticks(96);
    }
    run_ticks(96);
}

static void store_file(const char* name, const uint8_t* data, uint32_t size) {
    SS9FileRequest request __attribute__((aligned(4))) = {
        (uint32_t)(uintptr_t)name, (uint32_t)(uintptr_t)data, size,
        SS9_FILE_CREATE};
    if (call_sys(SS9_SYS_FILE_STORE, (uint32_t)(uintptr_t)&request) !=
        (int32_t)size)
        fail("file store");
}

/* Checks an .O produced by on-target asm exists and is non-empty. */
static void check_object(const char* name) {
    static uint8_t buffer[512];
    if (ss_system_file_load(name, buffer, sizeof(buffer)) <= 0)
        fail("object file missing");
}

int main(void) {
    tty_puts("START mux-flow\n");
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
    uint32_t free_before = ss_memory_free_bytes();

    SSModuleId edit_module;
    if (ss_module_register_image(
            ss_test_edit_image,
            (uint32_t)(ss_test_edit_image_end - ss_test_edit_image),
            SS_MODULE_STORAGE_DYNAMIC, &edit_module) != SS_OK)
        fail("register editor image");
    SSModuleId asm_module;
    if (ss_module_register_image(
            ss_test_asm_image,
            (uint32_t)(ss_test_asm_image_end - ss_test_asm_image),
            SS_MODULE_STORAGE_DYNAMIC, &asm_module) != SS_OK)
        fail("register assembler image");
    SSModuleId ld_module;
    if (ss_module_register_image(
            ss_test_ld_image,
            (uint32_t)(ss_test_ld_image_end - ss_test_ld_image),
            SS_MODULE_STORAGE_DYNAMIC, &ld_module) != SS_OK)
        fail("register linker image");

    /* MAIN.S and OBJ.S are pre-stored; START.S is created on screen 1 by
     * the editor so one link input is edited end to end on the mux. */
    store_file("MAIN.S", ss_test_link_main_src,
               (uint32_t)(ss_test_link_main_src_end - ss_test_link_main_src));
    store_file("OBJ.S", ss_test_link_obj_src,
               (uint32_t)(ss_test_link_obj_src_end - ss_test_link_obj_src));

    /* Model the supported 31-row text region. The editor must obtain this
     * live size through CONSOLE_SIZE while running inside the mux. */
    ss_test_console_offset = 0;
    ss_test_console_d2 = (79u << 16) | 30u;
    char mux_command[] = "mux";
    if (ss_cli_execute_line(mux_command) != 0)
        fail("mux command");
    if (call_sys(SS9_SYS_CONSOLE_SIZE, 0) !=
        (int32_t)((80u << 16) | 31u))
        fail("31-row mux console size");
    for (unsigned i = 0; i < 40; i++) tick();
    SSProcessId shells[2] = {0, 0};
    for (unsigned i = 0; i < SS_PROCESS_MAX; i++) {
        SSProcessSnapshot process;
        if (ss_process_snapshot_at(i, &process) == SS_OK && process.ppid == 1 &&
            process.name[0] == 's' && process.name[1] == 'h' &&
            process.console_id >= 1 && process.console_id <= 2)
            shells[process.console_id - 1] = process.pid;
    }
    if (!shells[0] || !shells[1] || shells[0] == shells[1])
        fail("mux shell consoles");

    /* Screen 1: the editor fails to read START.S, offers n/q, and the
     * typed 'n' starts a new buffer. The full link sample source is
     * typed, then saved with :wq. */
    const char* edit_argv[] = {"edit", "START.S"};
    SSProcessId edit_pid;
    uint16_t status;
    if (ss_process_spawn(1, edit_module, 2, edit_argv, &edit_pid) != SS_OK)
        fail("spawn editor");
    ss_cli_set_foreground_pid(edit_pid);
    push_key('n');
    run_ticks(20);
    if (!ss_test_console_locate_rows[29] || !ss_test_console_locate_rows[30])
        fail("editor 31-row status/command layout");
    push_key('i');
    type_editor_text(
        ss_test_link_start_src,
        (uint32_t)(ss_test_link_start_src_end - ss_test_link_start_src));
    push_key(27);
    run_ticks(96);
    push_key(':');
    push_key('w');
    push_key('q');
    push_key('\r');
    int16_t edit_wait = SS_ERR_STATE;
    for (unsigned i = 0; i < 40000 && edit_wait == SS_ERR_STATE; i++) {
        tick();
        edit_wait = ss_process_wait(1, edit_pid, &status);
    }
    ss_cli_set_foreground_pid(0);
    if (edit_wait != SS_OK || status != 0)
        fail("editor run");
    static uint8_t saved[512];
    uint32_t start_length =
        (uint32_t)(ss_test_link_start_src_end - ss_test_link_start_src);
    int32_t saved_size = ss_system_file_load("START.S", saved, sizeof(saved));
    if (saved_size != (int32_t)start_length)
        fail("editor saved size");
    for (uint32_t i = 0; i < start_length; i++)
        if (saved[i] != ss_test_link_start_src[i])
            fail("editor saved bytes");

    /* Screen 2: assemble the three inputs and link PROG.S9M, all typed
     * at the mux shell on console 1 and running against FAT16 files. */
    switch_screen(1);
    ss_test_console_count = 0;
    type_line("run asm START.S START.O");
    wait_console("exited with status 0");
    if (!console_contains("Wrote START.O") ||
        !console_contains("text 10, data 0, bss 0"))
        fail("screen 2 asm START.O");
    check_object("START.O");
    ss_test_console_count = 0;
    type_line("run asm MAIN.S MAIN.O");
    wait_console("exited with status 0");
    if (!console_contains("Wrote MAIN.O") ||
        !console_contains("text 44, data 16, bss 4"))
        fail("screen 2 asm MAIN.O");
    check_object("MAIN.O");
    ss_test_console_count = 0;
    type_line("run asm OBJ.S OBJ.O");
    wait_console("exited with status 0");
    if (!console_contains("Wrote OBJ.O") ||
        !console_contains("text 6, data 8, bss 0"))
        fail("screen 2 asm OBJ.O");
    check_object("OBJ.O");
    ss_test_console_count = 0;
    type_line("run ld PROG.S9M START.O MAIN.O OBJ.O");
    wait_console("exited with status 0");
    if (!console_contains("Wrote PROG.S9M"))
        fail("screen 2 ld PROG.S9M");

    /* The host linker golden over the same inputs is 180 bytes. */
    uint32_t expected_size =
        (uint32_t)(ss_test_link_expected_end - ss_test_link_expected);
    static uint8_t prog_image[512];
    if (expected_size != 180 || expected_size > sizeof(prog_image))
        fail("golden link size");
    if (ss_system_file_load("PROG.S9M", prog_image, sizeof(prog_image)) !=
        (int32_t)expected_size)
        fail("linked image size");
    for (uint32_t i = 0; i < expected_size; i++)
        if (prog_image[i] != ss_test_link_expected[i])
            fail("linked image bytes");

    /* Screen 3: PROG is not registered, so `run PROG` must resolve
     * through the shell's FAT16 fallback, print LK-free "LNK\n", and
     * exit with status 27 (main returns base 7 + factor 10 doubled). */
    switch_screen(2);
    ss_test_console_count = 0;
    type_line("run PROG");
    wait_console("exited with status 27\r\n");
    if (!console_contains("LNK\n"))
        fail("screen 3 run PROG output");
    ss_test_console_count = 0;
    type_line("unload PROG");
    wait_console("Module unloaded.");
    SSModuleId prog_module;
    if (ss_module_find("PROG", &prog_module) != SS_ERR_ID)
        fail("PROG unload registry");

    /* Mux shell lifecycle on screen 2: a background child is waited through
     * the interactive ss_cli_wait_pid path, the shell exits, screens 1 and
     * 3 keep answering, and the root `mux` reaps the zombie and respawns
     * the screen 2 shell. */
    switch_screen(1);
    ss_test_console_count = 0;
    type_line("help&");
    wait_console("Started command as PID ");
    SSProcessId builtin_pid = 0;
    for (unsigned i = 0; i < SS_PROCESS_MAX; i++) {
        SSProcessSnapshot child;
        if (ss_process_snapshot_at(i, &child) == SS_OK &&
            child.ppid == shells[0])
            builtin_pid = child.pid;
    }
    if (!builtin_pid)
        fail("screen 2 background child");
    run_ticks(80); /* let the help text finish before reusing the capture */
    ss_test_console_count = 0;
    type_wait_line(builtin_pid);
    wait_console("exited with status 0");
    ss_test_console_count = 0;
    type_line("exit");
    wait_console("SSOS-9 terminated.");
    unsigned became_zombie = 0;
    for (unsigned i = 0; i < 400 && !became_zombie; i++) {
        tick();
        SSProcessSnapshot exited_shell;
        if (ss_process_snapshot(shells[0], &exited_shell) == SS_OK &&
            exited_shell.state == SS_PROCESS_ZOMBIE)
            became_zombie = 1;
    }
    if (!became_zombie)
        fail("screen 2 shell zombie");

    /* Screens 1 and 3 must keep answering while screen 2 is closed. The
     * root CLI is this harness's PID 1 task, so it answers through a
     * direct ss_cli_execute_line, and screen 3 through its live shell. */
    switch_screen(0);
    ss_test_console_count = 0;
    char version_command[] = "version";
    if (ss_cli_execute_line(version_command) != 0 ||
        !console_contains("SSOS-9 CLI preemptive minimum"))
        fail("root shell after screen 2 exit");
    switch_screen(2);
    ss_test_console_count = 0;
    type_line("version");
    wait_console("SSOS-9 CLI preemptive minimum");

    /* The root `mux` reaps the zombie shell and respawns the window. */
    char mux_respawn[] = "mux";
    if (ss_cli_execute_line(mux_respawn) != 0)
        fail("mux respawn command");
    run_ticks(40);
    SSProcessId respawned[2] = {0, 0};
    for (unsigned i = 0; i < SS_PROCESS_MAX; i++) {
        SSProcessSnapshot process;
        if (ss_process_snapshot_at(i, &process) == SS_OK && process.ppid == 1 &&
            process.name[0] == 's' && process.name[1] == 'h' &&
            process.console_id >= 1 && process.console_id <= 2)
            respawned[process.console_id - 1] = process.pid;
    }
    if (!respawned[0] || respawned[0] == shells[0] || respawned[1] != shells[1])
        fail("mux zombie reap and respawn");
    shells[0] = respawned[0];
    switch_screen(1);
    ss_test_console_count = 0;
    type_line("version");
    wait_console("SSOS-9 CLI preemptive minimum");

    /* The line editor's delete branches through the real shell_loop:
     * "versioz" + BS -> "versio", 'n' -> "version", DEL undoes the n,
     * and retyping it lands on version again. 0x7f is the classic
     * spelling of delete; the X68000 keyboard's real DEL (scan 0x37)
     * runs in the next sequence. */
    ss_test_console_count = 0;
    type_line("versioz\bn\x7fn");
    wait_console("SSOS-9 CLI preemptive minimum");
    /* The real cursor keys and DEL (scan<<8): "version" + LEFT + 'z'
     * -> "versiozn", DEL removes the n, BS removes the z, and 'n' lands
     * on version again. */
    ss_test_console_count = 0;
    for (const char* p = "version"; *p; p++) push_key((uint8_t)*p);
    push_key(0x3B00);
    push_key('z');
    push_key(0x3700);
    push_key(0x08);
    push_key('n');
    push_key('\r');
    wait_console("SSOS-9 CLI preemptive minimum");

    /* Reclaim: reap the mux shells, unload the tools, and require the
     * arena to return to its pre-mux baseline. */
    if (ss_process_kill(shells[0]) != SS_OK ||
        ss_process_kill(shells[1]) != SS_OK)
        fail("mux shell kill");
    uint16_t shell_status;
    int16_t first_wait = SS_ERR_STATE, second_wait = SS_ERR_STATE;
    for (unsigned i = 0;
         i < 400 && (first_wait == SS_ERR_STATE || second_wait == SS_ERR_STATE);
         i++) {
        tick();
        if (first_wait == SS_ERR_STATE)
            first_wait = ss_process_wait(1, shells[0], &shell_status);
        if (second_wait == SS_ERR_STATE)
            second_wait = ss_process_wait(1, shells[1], &shell_status);
    }
    if (first_wait != SS_OK || second_wait != SS_OK)
        fail("mux shell reap");
    if (ss_module_unload(edit_module) != SS_OK ||
        ss_module_unload(asm_module) != SS_OK ||
        ss_module_unload(ld_module) != SS_OK)
        fail("module unload");
    if (ss_memory_free_bytes() != free_before)
        fail("arena baseline after unload");

    tty_puts("OK mux three-screen edit assemble link run\n");
    for (;;) tick();
}
