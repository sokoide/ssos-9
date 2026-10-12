/* Run one shared S9M1 code image with independent A5/GOT state. */

#include "../../../ssos/os/app/cli_wait.h"
#include "builtin.h"
#include "main_task.h"
#include "process.h"
#include "ssos9/syscall.h"
#include "syscall.h"
#include "system_arena.h"
#include "tty.h"

static SSTask main_tcb;
static uint8_t image[124];
static uint8_t fake_keys[8];
static uint8_t fake_key_head;
static uint8_t fake_key_count;
extern void ss_test_install_syscall_vector(void);
extern void ss_test_install_ctrl_c_vector(void);
extern int32_t ss_test_syscall_roundtrip(void);

int _iocs_b_keysns(void) { return fake_key_count; }

int _iocs_b_keyinp(void) {
    if (fake_key_count == 0)
        return 0;
    uint8_t key = fake_keys[fake_key_head++ % sizeof(fake_keys)];
    fake_key_count--;
    return key;
}

static void push_key(uint8_t key) {
    uint8_t tail =
        (uint8_t)((fake_key_head + fake_key_count) % sizeof(fake_keys));
    fake_keys[tail] = key;
    fake_key_count++;
}

static int32_t call_sys(uint32_t number, uint32_t request) {
    register uint32_t d0 __asm__("d0") = number;
    register uint32_t d1 __asm__("d1") = request;
    __asm__ volatile("trap #0" : "+d"(d0), "+d"(d1) : : "cc", "memory");
    return (int32_t)d0;
}

static void put16(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put32(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint32_t get32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t crc32(void) {
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0; i < sizeof(image); i++) {
        uint8_t value = i >= 0x34 && i < 0x38 ? 0 : image[i];
        crc ^= value;
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0);
    }
    return crc ^ 0xffffffffu;
}

static void make_image(void) {
    image[0] = 'S';
    image[1] = '9';
    image[2] = 'M';
    image[3] = '1';
    put16(&image[4], 1);
    put16(&image[6], SS_MODULE_IMAGE_HEADER_SIZE);
    put32(&image[8], SS_MODULE_FLAG_PIC | SS_MODULE_FLAG_REENTRANT);
    put32(&image[0x0c], sizeof(image));
    put32(&image[0x10], 80);
    put32(&image[0x14], 8);
    put32(&image[0x18], 80);
    put32(&image[0x1c], 88);
    put32(&image[0x20], 16);
    put32(&image[0x24], 4);
    put32(&image[0x28], 4096);
    put32(&image[0x2c], 104);
    put16(&image[0x30], 8);
    put32(&image[0x38], 0);
    put32(&image[0x3c], 4);
    put32(&image[0x40], 112);
    put32(&image[0x44], 1);
    for (unsigned i = 0; i < 8; i++) image[104 + i] = (uint8_t)"qcounter"[i];

    /* movea.l (a5),a0; addq.l #1,(a0); trap #1; bra.s back to start. */
    image[80] = 0x20;
    image[81] = 0x55;
    image[82] = 0x52;
    image[83] = 0x90;
    image[84] = 0x4e;
    image[85] = 0x41;
    image[86] = 0x60;
    image[87] = 0xf8;
    put32(&image[112], 0);
    put32(&image[116], SS_MODULE_FIXUP_DATA32);
    put32(&image[120], 8);
    put32(&image[0x34], crc32());
}

static void make_busy_image(void) {
    make_image();
    /* QEMU's trap #1 simulates Timer D; this module makes no syscalls. */
    put16(&image[0x30], 5);
    image[104] = 'q';
    image[105] = 'b';
    image[106] = 'u';
    image[107] = 's';
    image[108] = 'y';
    image[109] = '\0';
    image[110] = '\0';
    image[111] = '\0';
    put32(&image[0x34], crc32());
}

static void make_sleep_image(void) {
    make_image();
    image[104] = 'q';
    image[105] = 's';
    image[106] = 'l';
    image[107] = 'e';
    image[108] = 'e';
    image[109] = 'p';
    image[110] = '0';
    image[111] = '0';
    /* SYS_SLEEP(127), TRAP #0, then retry after wakeup. */
    image[80] = 0x70;
    image[81] = SS9_SYS_SLEEP;
    image[82] = 0x72;
    image[83] = 127;
    image[84] = 0x4e;
    image[85] = 0x40;
    image[86] = 0x60;
    image[87] = 0xf8;
    put32(&image[0x34], crc32());
}

static void fail(const char* reason) {
    tty_puts("FAIL ");
    tty_puts(reason);
    tty_puts("\n");
    for (;;) __asm__ volatile("trap #1" ::: "memory");
}

static void tick(void) { __asm__ volatile("trap #1" ::: "memory"); }

static void set_counter(void* data, uint32_t value) {
    put32((uint8_t*)data + 8, value);
}

static void run_ticks(unsigned count) {
    for (unsigned i = 0; i < count; i++) tick();
}

static volatile uint32_t kernel_child_value;
static volatile SSProcessId kernel_child_pid;

static void* kernel_child_entry(void* argument) {
    kernel_child_value = *(const uint32_t*)argument;
    kernel_child_pid = ss_process_current_id();
    ss_process_exit_current(23);
    return NULL;
}

int main(void) {
    tty_puts("START process-spawn\n");
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
    if (call_sys(SS9_SYS_VERSION, 0) != SS9_ABI_VERSION ||
        call_sys(SS9_SYS_GETPID, 0) != 1 || call_sys(SS9_SYS_GETPPID, 0) != 0 ||
        call_sys(SS9_SYS_SLEEP, SS_MAX_SLEEP_TICKS + 1u) != SS_ERR_PARAM ||
        call_sys(SS9_SYS_EXIT, 0) != SS9_ERR_PARAM ||
        call_sys(0xffffffffu, 0) != SS9_ERR_NOSYS)
        fail("syscall ABI basics");

    make_image();
    SSModuleId module_id;
    if (ss_module_register_image(image, sizeof(image),
                                 SS_MODULE_STORAGE_BUILTIN,
                                 &module_id) != SS_OK)
        fail("module register");

    const char* argv[] = {"qcounter"};
    SSProcessId first_pid;
    SSProcessId second_pid;
    uint32_t free_before = ss_memory_free_bytes();
    if (ss_process_spawn(1, module_id, 1, argv, &first_pid) != SS_OK ||
        ss_process_spawn(1, module_id, 1, argv, &second_pid) != SS_OK)
        fail("spawn pair");
    SSProcessSnapshot first;
    SSProcessSnapshot second;
    if (ss_process_snapshot(first_pid, &first) != SS_OK ||
        ss_process_snapshot(second_pid, &second) != SS_OK ||
        first.module_id != second.module_id || first.module_id != module_id ||
        first.data_base == second.data_base ||
        first.got_base == second.got_base ||
        first.stack_low == second.stack_low)
        fail("private regions not separated");

    set_counter(first.data_base, 1000);
    if (get32(second.data_base + 8) != 0)
        fail("data area aliases");
    run_ticks(30);
    if (get32(first.data_base + 8) <= 1000 || get32(second.data_base + 8) == 0)
        fail("shared PIC failed to use private GOT");
    if (ss_process_snapshot(first_pid, &first) != SS_OK)
        fail("first snapshot after run");
    if (ss_process_snapshot(second_pid, &second) != SS_OK ||
        first.state != SS_PROCESS_READY || second.state != SS_PROCESS_READY)
        fail("running process was reaped");

    SS9ProcessInfo info;
    SS9ProcessInfoRequest info_request = {first_pid,
                                          (uint32_t)(uintptr_t)&info};
    SS9ProcessInfo entries[SS_PROCESS_MAX];
    SS9ProcessListRequest list_request = {SS_PROCESS_MAX, 0,
                                          (uint32_t)(uintptr_t)entries};
    if (call_sys(SS9_SYS_PROC_INFO, (uint32_t)(uintptr_t)&info_request) !=
            SS_OK ||
        call_sys(SS9_SYS_PROC_INFO, (uint32_t)(uintptr_t)&info_request + 1u) !=
            SS9_ERR_PARAM ||
        info.pid != first_pid || info.ppid != 1 ||
        call_sys(SS9_SYS_PROC_LIST, (uint32_t)(uintptr_t)&list_request) != 3 ||
        list_request.count != 3)
        fail("syscall process queries");

    const char* syscall_name = "qcounter";
    const char* syscall_argv[] = {"qcounter"};
    SS9SpawnRequest spawn_request = {(uint32_t)(uintptr_t)syscall_name, 1, 0,
                                     (uint32_t)(uintptr_t)syscall_argv};
    int32_t spawned_pid =
        call_sys(SS9_SYS_SPAWN, (uint32_t)(uintptr_t)&spawn_request);
    if (spawned_pid <= 1)
        fail("syscall spawn");

    uint16_t status;
    SS9WaitRequest wait_request = {first_pid, 0, 0};
    if (call_sys(SS9_SYS_KILL, first_pid) != SS_OK)
        fail("syscall kill first");
    if (call_sys(SS9_SYS_WAIT, (uint32_t)(uintptr_t)&wait_request) !=
            (int32_t)first_pid ||
        wait_request.exit_status != 0)
        fail("syscall wait first");
    if (ss_process_kill(second_pid) != SS_OK)
        fail("kill second");
    int16_t wait_status = SS_ERR_STATE;
    for (unsigned i = 0; i < 32 && wait_status == SS_ERR_STATE; i++) {
        wait_status = ss_process_wait(1, second_pid, &status);
        if (wait_status == SS_ERR_STATE)
            tick();
    }
    if (wait_status != SS_OK || status != 0)
        fail("reap/wait second");
    SS9WaitRequest syscall_wait = {(uint32_t)spawned_pid, 0, 0};
    if (call_sys(SS9_SYS_KILL, (uint32_t)spawned_pid) != SS_OK ||
        call_sys(SS9_SYS_WAIT, (uint32_t)(uintptr_t)&syscall_wait) !=
            spawned_pid ||
        syscall_wait.exit_status != 0 || ss_memory_free_bytes() != free_before)
        fail("syscall spawn/kill/wait reclaim");

    SSProcessId parent_pid;
    SSProcessId orphan_pid;
    if (ss_process_spawn(1, module_id, 1, argv, &parent_pid) != SS_OK ||
        ss_process_spawn(parent_pid, module_id, 1, argv, &orphan_pid) !=
            SS_OK ||
        ss_process_kill(orphan_pid) != SS_OK)
        fail("orphan setup");
    run_ticks(24);
    if (ss_process_kill(parent_pid) != SS_OK)
        fail("orphan parent kill");
    run_ticks(24);
    if (ss_process_snapshot(orphan_pid, &second) != SS_OK || second.ppid != 1 ||
        ss_process_wait(1, orphan_pid, &status) != SS_OK ||
        ss_process_wait(1, parent_pid, &status) != SS_OK)
        fail("orphan adoption/reap");

    make_sleep_image();
    SSModuleId sleep_module;
    if (ss_module_register_image(image, sizeof(image),
                                 SS_MODULE_STORAGE_BUILTIN,
                                 &sleep_module) != SS_OK)
        fail("sleep module register");
    const char* sleep_argv[] = {"qsleep00"};
    SSProcessId sleeper_pid;
    if (ss_process_spawn(1, sleep_module, 1, sleep_argv, &sleeper_pid) != SS_OK)
        fail("sleep process spawn");
    run_ticks(8);
    if (ss_process_snapshot(sleeper_pid, &second) != SS_OK ||
        second.task_id == 0 ||
        tcb_table[second.task_id - 1].state != SS_TS_WAIT)
        fail("sleep process did not block");
    ss_cli_set_foreground_pid(sleeper_pid);
    __asm__ volatile("trap #13" ::: "cc", "memory");
    int interrupted = 0;
    if (ss_cli_wait_pid(sleeper_pid, &status, &interrupted) != SS_OK ||
        !interrupted || ss_process_snapshot(sleeper_pid, &second) != SS_ERR_ID)
        fail("foreground Ctrl-C kill and reap");

    make_busy_image();
    SSModuleId busy_module;
    if (ss_module_register_image(image, sizeof(image),
                                 SS_MODULE_STORAGE_BUILTIN,
                                 &busy_module) != SS_OK)
        fail("busy module register");
    const char* busy_argv[] = {"qbusy"};
    SSProcessId busy_pid;
    if (ss_process_spawn(1, busy_module, 1, busy_argv, &busy_pid) != SS_OK)
        fail("busy process spawn");
    if (ss_process_snapshot(busy_pid, &second) != SS_OK)
        fail("busy process snapshot");
    run_ticks(20);
    if (get32(second.data_base + 8) == 0)
        fail("busy process did not run");
    ss_cli_set_foreground_pid(busy_pid);
    __asm__ volatile("trap #13" ::: "cc", "memory");
    interrupted = 0;
    if (ss_cli_wait_pid(busy_pid, &status, &interrupted) != SS_OK ||
        !interrupted || ss_process_snapshot(busy_pid, &second) != SS_ERR_ID)
        fail("Ctrl-C kills non-cooperative busy process");

    SSProcessId background_pid;
    if (ss_process_spawn(1, sleep_module, 1, sleep_argv, &background_pid) !=
        SS_OK)
        fail("background sleep process spawn");
    run_ticks(8);
    ss_cli_set_foreground_pid(0);
    push_key('x');
    push_key(0x03);
    if (ss_cli_wait_pid(background_pid, &status, NULL) != SS_CLI_WAIT_CANCELLED)
        fail("background Ctrl-C did not cancel wait");
    if (ss_cli_next_key() != 'x')
        fail("background wait lost ordinary key");
    if (ss_process_snapshot(background_pid, &second) != SS_OK ||
        (tcb_table[second.task_id - 1].state != SS_TS_WAIT &&
         tcb_table[second.task_id - 1].state != SS_TS_READY))
        fail("background wait Ctrl-C killed child");
    if (ss_process_kill(background_pid) != SS_OK ||
        ss_cli_wait_pid(background_pid, &status, NULL) != SS_OK)
        fail("background cleanup after cancelled wait");

    uint32_t copied_argument = 42;
    uint32_t kernel_free_before = ss_memory_free_bytes();
    SSProcessId kernel_pid;
    if (ss_process_spawn_kernel(1, "builtin", kernel_child_entry,
                                &copied_argument, sizeof(copied_argument),
                                &kernel_pid) != SS_OK)
        fail("kernel child spawn");
    copied_argument = 99;
    run_ticks(32);
    if (kernel_child_value != 42 || kernel_child_pid != kernel_pid ||
        ss_process_wait(1, kernel_pid, &status) != SS_OK || status != 23 ||
        ss_memory_free_bytes() != kernel_free_before)
        fail("kernel child copied argument/return/reap");

    tty_puts("OK shared code/private A5 data\n");
    for (;;) tick();
}
