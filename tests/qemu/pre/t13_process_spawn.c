/* Run one shared S9M1 code image with independent A5/GOT state. */

#include "../../../ssos/os/app/cli.h"
#include "../../../ssos/os/app/cli_console.h"
#include "../../../ssos/os/app/cli_wait.h"
#include "../../../ssos/os/fs/fat12.h"
#include "../../../ssos/os/fs/scsi_volume.h"
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
extern uint8_t ss_test_console_bytes[256];
extern uint32_t ss_test_console_count;
extern const uint8_t ss_test_hello_image[], ss_test_hello_image_end[];
extern const uint8_t ss_test_edit_image[], ss_test_edit_image_end[];
extern const uint8_t ss_test_asm_image[], ss_test_asm_image_end[];
extern const uint8_t ss_test_asm_source[], ss_test_asm_source_end[];
extern const uint8_t ss_test_asm_expected[], ss_test_asm_expected_end[];
static uint8_t fat_disk[8][512];
extern const uint8_t ss_test_data_disk[];
static int32_t fail_disk_sector = -1;
static uint32_t disk_read_calls;
static uint8_t scsi_test_disk[50][512];
static int scsi_test_read(void* context, uint32_t sector, uint8_t* out) {
    (void)context;
    if (sector >= 50)
        return -1;
    for (unsigned i = 0; i < 512; i++) out[i] = scsi_test_disk[sector][i];
    return 0;
}
static int scsi_test_write(void* context, uint32_t sector, const uint8_t* in) {
    (void)context;
    if (sector >= 50)
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
    disk_read_calls++;
    unsigned pos = (unsigned)position;
    unsigned track = (pos >> 16) & 0xffu;
    unsigned side = (pos >> 8) & 0xffu;
    unsigned number = pos & 0xffu;
    if (mode != 0x9170 || (pos >> 24) != 3 || track >= 77 || side > 1 ||
        number < 1 || number > 8 || size != 1024)
        return -1;
    unsigned sector = (track * 2 + side) * 8 + number - 1;
    if ((int32_t)sector == fail_disk_sector)
        return -1;
    for (unsigned i = 0; i < 1024; i++)
        ((uint8_t*)out)[i] = ss_test_data_disk[sector * 1024 + i];
    return 0;
}

static int fat_read_sector(void* context, uint32_t sector, uint8_t* out) {
    (void)context;
    if (sector >= 8)
        return -1;
    for (unsigned i = 0; i < 512; i++) out[i] = fat_disk[sector][i];
    return 0;
}

typedef struct {
    const uint8_t* current;
    const uint8_t* end;
} HelloReader;

static int32_t hello_read(void* context, uint8_t* out, uint32_t size) {
    HelloReader* reader = context;
    uint32_t remaining = (uint32_t)(reader->end - reader->current);
    if (size > remaining)
        size = remaining;
    for (uint32_t i = 0; i < size; i++) out[i] = *reader->current++;
    return (int32_t)size;
}

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

    /* Aligned addresses in vectors, VRAM/I/O, or beyond supported RAM
     * must fail before any load/store through the request pointer. */
    const uint32_t invalid[] = {0, 0x80, 0x00c00000u, 0x00e88000u, 0xfffffffcu};
    const uint32_t pointer_calls[] = {SS9_SYS_SPAWN, SS9_SYS_WAIT,
                                      SS9_SYS_PROC_INFO, SS9_SYS_PROC_LIST};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        for (unsigned j = 0;
             j < sizeof(pointer_calls) / sizeof(pointer_calls[0]); j++)
            if (call_sys(pointer_calls[j], invalid[i]) != SS9_ERR_PARAM)
                fail("unsafe syscall request accepted");
    SS9ProcessInfoRequest bad_output
        __attribute__((aligned(4))) = {1, 0x00bffffcu};
    if (call_sys(SS9_SYS_PROC_INFO, (uint32_t)(uintptr_t)&bad_output) !=
        SS9_ERR_PARAM)
        fail("output crossing RAM end accepted");
    SS9SpawnRequest bad_spawn
        __attribute__((aligned(4))) = {0x00c00000u, 0, 0, 0};
    if (call_sys(SS9_SYS_SPAWN, (uint32_t)(uintptr_t)&bad_spawn) !=
        SS9_ERR_PARAM)
        fail("invalid module name accepted");
    bad_spawn.name = (uint32_t)(uintptr_t)"counter";
    bad_spawn.argc = 1;
    bad_spawn.argv = 0x00c00000u;
    if (call_sys(SS9_SYS_SPAWN, (uint32_t)(uintptr_t)&bad_spawn) !=
        SS9_ERR_PARAM)
        fail("invalid argv accepted");
    uint32_t bad_string = 0x00c00000u;
    bad_spawn.argv = (uint32_t)(uintptr_t)&bad_string;
    if (call_sys(SS9_SYS_SPAWN, (uint32_t)(uintptr_t)&bad_spawn) !=
        SS9_ERR_PARAM)
        fail("invalid argument string accepted");

    char unterminated[SS_PROCESS_MAX_ARG_BYTES];
    for (unsigned i = 0; i < sizeof(unterminated); i++) unterminated[i] = 'x';
    bad_spawn.name = (uint32_t)(uintptr_t)unterminated;
    bad_spawn.argc = 0;
    if (call_sys(SS9_SYS_SPAWN, (uint32_t)(uintptr_t)&bad_spawn) !=
        SS9_ERR_PARAM)
        fail("unterminated name accepted");
    bad_spawn.name = (uint32_t)(uintptr_t)"counter";
    bad_spawn.argc = 1;
    bad_string = (uint32_t)(uintptr_t)unterminated;
    if (call_sys(SS9_SYS_SPAWN, (uint32_t)(uintptr_t)&bad_spawn) !=
        SS9_ERR_PARAM)
        fail("unterminated argument accepted");
    SS9ProcessListRequest bad_list
        __attribute__((aligned(4))) = {2, 0, 0x00bffffcu};
    if (call_sys(SS9_SYS_PROC_LIST, (uint32_t)(uintptr_t)&bad_list) !=
        SS9_ERR_PARAM)
        fail("list crossing RAM end accepted");

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

    /* Isolate buffer policy with IRQs masked; these dispatches do not sleep.
     * Restore the real current task before scheduling either PIC process. */
    uint16_t policy_sr = ss_irq_save();
    ss_curr_task = &tcb_table[first.task_id - 1];
    SS9ProcessInfoRequest* private_request =
        (SS9ProcessInfoRequest*)first.stack_low;
    private_request->pid = first_pid;
    private_request->info = (uint32_t)(uintptr_t)first.stack_low + 16u;
    int32_t private_result = ss_syscall_dispatch(
        SS9_SYS_PROC_INFO, (uint32_t)(uintptr_t)private_request);
    private_request->info = (uint32_t)(uintptr_t)second.stack_low;
    int32_t foreign_result = ss_syscall_dispatch(
        SS9_SYS_PROC_INFO, (uint32_t)(uintptr_t)private_request);
    private_request->info = (uint32_t)(uintptr_t)image;
    int32_t image_result = ss_syscall_dispatch(
        SS9_SYS_PROC_INFO, (uint32_t)(uintptr_t)private_request);
    int32_t foreign_request = ss_syscall_dispatch(
        SS9_SYS_PROC_INFO, (uint32_t)(uintptr_t)second.stack_low);
    ss_curr_task = &main_tcb;
    ss_irq_restore(policy_sr);
    if (private_result != SS_OK || foreign_result != SS9_ERR_PARAM ||
        image_result != SS9_ERR_PARAM || foreign_request != SS9_ERR_PARAM)
        fail("private syscall buffer policy");

    uint8_t write_data[] = {'H', 0, 'i', '\n'};
    SS9WriteRequest write_req __attribute__((aligned(4))) = {
        1, (uint32_t)(uintptr_t)write_data, sizeof(write_data)};
    ss_test_console_count = 0;
    if (call_sys(SS9_SYS_WRITE, (uint32_t)(uintptr_t)&write_req) != 4 ||
        ss_test_console_count != 4 || ss_test_console_bytes[0] != 'H' ||
        ss_test_console_bytes[1] != 0 || ss_test_console_bytes[2] != 'i' ||
        ss_test_console_bytes[3] != '\n')
        fail("write bytes");
    write_req.fd = 2;
    if (call_sys(SS9_SYS_WRITE, (uint32_t)(uintptr_t)&write_req) !=
        SS9_ERR_PARAM)
        fail("write fd");
    write_req.fd = 1;
    write_req.length = SS9_WRITE_MAX + 1u;
    if (call_sys(SS9_SYS_WRITE, (uint32_t)(uintptr_t)&write_req) !=
        SS9_ERR_PARAM)
        fail("write length");
    write_req.buffer = 0xffffffffu;
    write_req.length = 2;
    if (call_sys(SS9_SYS_WRITE, (uint32_t)(uintptr_t)&write_req) !=
        SS9_ERR_PARAM)
        fail("write range");
    write_req.length = 0;
    if (call_sys(SS9_SYS_WRITE, (uint32_t)(uintptr_t)&write_req) != 0 ||
        call_sys(SS9_SYS_WRITE, (uint32_t)(uintptr_t)&write_req + 1u) !=
            SS9_ERR_PARAM ||
        ss_test_console_count != 4)
        fail("write empty or unaligned request");
    if (call_sys(SS9_SYS_CONSOLE_CLEAR, 0) != 0 ||
        call_sys(SS9_SYS_CONSOLE_CLEAR, 1) != SS9_ERR_PARAM ||
        call_sys(SS9_SYS_CONSOLE_LOCATE, (79u << 16) | 24u) != 0 ||
        call_sys(SS9_SYS_CONSOLE_LOCATE, 80u << 16) != SS9_ERR_PARAM ||
        call_sys(SS9_SYS_CONSOLE_LOCATE, 25u) != SS9_ERR_PARAM)
        fail("console position syscall");

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

    SS9ProcessInfo info __attribute__((aligned(4)));
    SS9ProcessInfoRequest info_request
        __attribute__((aligned(4))) = {first_pid, (uint32_t)(uintptr_t)&info};
    SS9ProcessInfo entries[SS_PROCESS_MAX] __attribute__((aligned(4)));
    SS9ProcessListRequest list_request __attribute__((aligned(4))) = {
        SS_PROCESS_MAX, 0, (uint32_t)(uintptr_t)entries};
    if (call_sys(SS9_SYS_PROC_INFO, (uint32_t)(uintptr_t)&info_request) !=
            SS_OK ||
        call_sys(SS9_SYS_PROC_INFO, (uint32_t)(uintptr_t)&info_request + 1u) !=
            SS9_ERR_PARAM ||
        info.pid != first_pid || info.ppid != 1 ||
        call_sys(SS9_SYS_PROC_LIST, (uint32_t)(uintptr_t)&list_request) != 3 ||
        list_request.count != 3)
        fail("syscall process queries");

    const char* syscall_name = "qcounter";
    const char* syscall_argv[] __attribute__((aligned(4))) = {"qcounter"};
    SS9SpawnRequest spawn_request
        __attribute__((aligned(4))) = {(uint32_t)(uintptr_t)syscall_name, 1, 0,
                                       (uint32_t)(uintptr_t)syscall_argv};
    int32_t spawned_pid =
        call_sys(SS9_SYS_SPAWN, (uint32_t)(uintptr_t)&spawn_request);
    if (spawned_pid <= 1)
        fail("syscall spawn");

    uint16_t status;
    SS9WaitRequest wait_request __attribute__((aligned(4))) = {first_pid, 0, 0};
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
    SS9WaitRequest syscall_wait
        __attribute__((aligned(4))) = {(uint32_t)spawned_pid, 0, 0};
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
    ss_cli_set_foreground_pid(1);
    push_key('k');
    if (call_sys(SS9_SYS_CONSOLE_KEY, 1) != SS9_ERR_PARAM ||
        call_sys(SS9_SYS_CONSOLE_KEY, 0) != 'k' ||
        call_sys(SS9_SYS_CONSOLE_KEY, 0) != -1)
        fail("foreground key syscall");
    ss_cli_set_foreground_pid(0);

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

    uint32_t hello_free_before = ss_memory_free_bytes();
    HelloReader hello_reader = {ss_test_hello_image, ss_test_hello_image_end};
    SSModuleId hello_module;
    if (ss_module_load_reader(
            hello_read, &hello_reader,
            (uint32_t)(ss_test_hello_image_end - ss_test_hello_image),
            &hello_module) != SS_OK)
        fail("hello load");
    SSProcessId hello_pid;
    ss_test_console_count = 0;
    if (ss_process_spawn(1, hello_module, 0, NULL, &hello_pid) != SS_OK)
        fail("hello spawn");
    run_ticks(32);
    if (ss_cli_wait_pid(hello_pid, &status, NULL) != SS_OK || status != 0 ||
        ss_test_console_count != 6 || ss_test_console_bytes[0] != 'H' ||
        ss_test_console_bytes[1] != 'e' || ss_test_console_bytes[2] != 'l' ||
        ss_test_console_bytes[3] != 'l' || ss_test_console_bytes[4] != 'o' ||
        ss_test_console_bytes[5] != '\n')
        fail("hello output or wait");
    if (ss_module_unload(hello_module) != SS_OK ||
        ss_memory_free_bytes() != hello_free_before)
        fail("hello cleanup");

    char command[] = "run hello &";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(command) != 0 ||
        ss_module_find("hello", &hello_module) != SS_OK)
        fail("shell hello command");
    char busy_unload[] = "unload hello";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(busy_unload) != 0 ||
        !console_contains("Unable to unload module.") ||
        ss_module_find("hello", &hello_module) != SS_OK)
        fail("shell unload active module");
    ss_test_console_count = 0;
    run_ticks(32);
    if (ss_test_console_count != 6 || ss_test_console_bytes[0] != 'H' ||
        ss_test_console_bytes[1] != 'e' || ss_test_console_bytes[2] != 'l' ||
        ss_test_console_bytes[3] != 'l' || ss_test_console_bytes[4] != 'o' ||
        ss_test_console_bytes[5] != '\n')
        fail("shell hello output");
    SSProcessId shell_hello_pid = 0;
    for (uint16_t i = 0; i < SS_PROCESS_MAX; i++) {
        SSProcessSnapshot child;
        if (ss_process_snapshot_at(i, &child) == SS_OK &&
            child.module_id == hello_module)
            shell_hello_pid = child.pid;
    }
    if (!shell_hello_pid ||
        ss_process_wait(1, shell_hello_pid, &status) != SS_OK || status != 0)
        fail("shell background cleanup");

    char foreground_command[] = "run hello";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(foreground_command) != 0 ||
        !console_contains("Hello\n") ||
        !console_contains("exited with status 0\r\n"))
        fail("shell foreground hello or return");
    char unload_command[] = "unload hello";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(unload_command) != 0 ||
        !console_contains("Module unloaded.") ||
        ss_module_find("hello", &hello_module) != SS_ERR_ID ||
        ss_memory_free_bytes() != hello_free_before)
        fail("shell hello cleanup");

    uint32_t hello_size =
        (uint32_t)(ss_test_hello_image_end - ss_test_hello_image);
    if (hello_size > 512)
        fail("hello image exceeds fixture cluster");
    fat_disk[0][12] = 2;
    fat_disk[0][13] = 1;
    fat_disk[0][14] = 1;
    fat_disk[0][16] = 1;
    fat_disk[0][17] = 16;
    fat_disk[0][19] = 8;
    fat_disk[0][22] = 1;
    fat_disk[0][510] = 0x55;
    fat_disk[0][511] = 0xaa;
    fat_disk[1][3] = 0xff;
    fat_disk[1][4] = 0x0f;
    const char disk_name[] = "HELLO   S9M";
    for (unsigned i = 0; i < 11; i++) fat_disk[2][i] = disk_name[i];
    fat_disk[2][26] = 2;
    fat_disk[2][28] = (uint8_t)hello_size;
    fat_disk[2][29] = (uint8_t)(hello_size >> 8);
    for (uint32_t i = 0; i < hello_size; i++)
        fat_disk[3][i] = ss_test_hello_image[i];
    SSFat12 volume;
    SSFat12File file;
    if (ss_fat12_mount(&volume, fat_read_sector, NULL, 8) ||
        ss_fat12_open(&volume, "hello.s9m", &file) ||
        ss_module_load_reader(ss_fat12_read, &file, file.size, &hello_module) !=
            SS_OK)
        fail("FAT hello load");
    ss_test_console_count = 0;
    char disk_command[] = "run hello";
    if (ss_cli_execute_line(disk_command) != 0 ||
        !console_contains("Hello\n") ||
        !console_contains("exited with status 0\r\n"))
        fail("FAT hello execution");
    if (ss_module_unload(hello_module) != SS_OK ||
        ss_memory_free_bytes() != hello_free_before)
        fail("FAT hello cleanup");

    char invalid_drive_command[] = "load 4 HELLO.S9M";
    ss_test_console_count = 0;
    disk_read_calls = 0;
    if (ss_cli_execute_line(invalid_drive_command) != 0 ||
        !console_contains("Invalid 2HD drive.") || disk_read_calls != 0 ||
        ss_memory_free_bytes() != hello_free_before)
        fail("invalid disk drive accepted");
    char bad_disk_command[] = "load 0 HELLO.S9M";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(bad_disk_command) != 0 ||
        !console_contains("Unable to load module: disk or FAT12.") ||
        ss_memory_free_bytes() != hello_free_before ||
        ss_module_find("hello", &hello_module) != SS_ERR_ID)
        fail("disk read failure cleanup");
    char missing_file_command[] = "load 1 MISSING.S9M";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(missing_file_command) != 0 ||
        !console_contains("Unable to load module: file or cluster chain.") ||
        ss_memory_free_bytes() != hello_free_before ||
        ss_module_find("hello", &hello_module) != SS_ERR_ID)
        fail("missing disk file cleanup");
    fail_disk_sector = 11; /* Generated disk's first data sector. */
    char failing_file_command[] = "load 1 HELLO.S9M";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(failing_file_command) != 0 ||
        !console_contains(
            "Unable to load module: image, memory, or registry.") ||
        ss_memory_free_bytes() != hello_free_before ||
        ss_module_find("hello", &hello_module) != SS_ERR_ID)
        fail("disk file read failure cleanup");
    fail_disk_sector = -1;
    char load_command[] = "load 1 HELLO.S9M";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(load_command) != 0 ||
        !console_contains("Module loaded.") ||
        ss_module_find("hello", &hello_module) != SS_OK)
        fail("disk CLI load");
    uint32_t loaded_free = ss_memory_free_bytes();
    SSModuleId first_loaded = hello_module;
    char load_again[] = "load 1 HELLO.S9M";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(load_again) != 0 ||
        !console_contains("Module loaded.") ||
        ss_module_find("hello", &hello_module) != SS_OK ||
        hello_module != first_loaded || ss_memory_free_bytes() != loaded_free)
        fail("disk duplicate load cleanup");
    ss_test_console_count = 0;
    char run_loaded[] = "run hello";
    if (ss_cli_execute_line(run_loaded) != 0 || !console_contains("Hello\n") ||
        !console_contains("exited with status 0"))
        fail("disk CLI run");
    if (ss_module_unload(hello_module) != SS_OK ||
        ss_memory_free_bytes() != hello_free_before)
        fail("disk CLI cleanup");

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
    char scsi_mount[] = "smount 3 10";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(scsi_mount) || !console_contains("FAT16 mounted."))
        fail("SCSI FAT16 mount");
    char scsi_write[] = "swrite MSG.TXT hello";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(scsi_write) || !console_contains("File written."))
        fail("SCSI FAT16 write");
    char scsi_read[] = "sread MSG.TXT";
    ss_test_console_count = 0;
    if (ss_cli_execute_line(scsi_read) || !console_contains("hello\r\n"))
        fail("SCSI FAT16 read");
    char file_syscall_name[] = "ABI.TXT";
    uint8_t syscall_data[8] = {'a', 'b', 'c', 'd'};
    SS9FileRequest file_request __attribute__((aligned(4))) = {
        (uint32_t)(uintptr_t)file_syscall_name,
        (uint32_t)(uintptr_t)syscall_data, 4, SS9_FILE_CREATE};
    if (call_sys(SS9_SYS_FILE_STORE, (uint32_t)(uintptr_t)&file_request) != 4)
        fail("FAT16 store syscall");
    for (unsigned i = 0; i < 8; i++) syscall_data[i] = 0;
    file_request.size = sizeof(syscall_data);
    file_request.flags = 0;
    if (call_sys(SS9_SYS_FILE_LOAD, (uint32_t)(uintptr_t)&file_request) != 4 ||
        syscall_data[0] != 'a' || syscall_data[3] != 'd')
        fail("FAT16 load syscall");
    SS9OpenRequest open_request __attribute__((aligned(4))) = {
        (uint32_t)(uintptr_t)file_syscall_name, SS9_OPEN_READ};
    int32_t fd = call_sys(SS9_SYS_OPEN, (uint32_t)(uintptr_t)&open_request);
    if (fd < 3)
        fail("file open syscall");
    SS9ReadRequest read_request __attribute__((aligned(4))) = {
        (uint32_t)fd, (uint32_t)(uintptr_t)syscall_data, 2};
    read_request.buffer = 0;
    if (call_sys(SS9_SYS_READ, (uint32_t)(uintptr_t)&read_request) !=
        SS9_ERR_PARAM)
        fail("file read invalid buffer");
    read_request.length = 0;
    if (call_sys(SS9_SYS_READ, (uint32_t)(uintptr_t)&read_request) != 0)
        fail("file read empty buffer");
    read_request.buffer = (uint32_t)(uintptr_t)syscall_data;
    read_request.length = 2;
    syscall_data[0] = syscall_data[1] = 0;
    if (call_sys(SS9_SYS_READ, (uint32_t)(uintptr_t)&read_request) != 2 ||
        syscall_data[0] != 'a' || syscall_data[1] != 'b')
        fail("first file read syscall");
    if (call_sys(SS9_SYS_READ, (uint32_t)(uintptr_t)&read_request) != 2 ||
        syscall_data[0] != 'c' || syscall_data[1] != 'd' ||
        call_sys(SS9_SYS_READ, (uint32_t)(uintptr_t)&read_request) != 0)
        fail("file read syscall EOF");
    if (call_sys(SS9_SYS_CLOSE, (uint32_t)fd) != 0 ||
        call_sys(SS9_SYS_READ, (uint32_t)(uintptr_t)&read_request) != SS9_ERR)
        fail("file close syscall");
    open_request.flags = 3;
    if (call_sys(SS9_SYS_OPEN, (uint32_t)(uintptr_t)&open_request) !=
        SS9_ERR_PARAM)
        fail("file open flags");
    open_request.flags = SS9_OPEN_REPLACE;
    fd = call_sys(SS9_SYS_OPEN, (uint32_t)(uintptr_t)&open_request);
    if (fd < 3)
        fail("file writer open");
    SS9WriteRequest file_write __attribute__((aligned(4))) = {
        (uint32_t)fd, (uint32_t)(uintptr_t)syscall_data, 2};
    syscall_data[0] = 'x';
    syscall_data[1] = 'y';
    if (call_sys(SS9_SYS_WRITE, (uint32_t)(uintptr_t)&file_write) != 2 ||
        call_sys(SS9_SYS_WRITE, (uint32_t)(uintptr_t)&file_write) != 2 ||
        call_sys(SS9_SYS_CLOSE, (uint32_t)fd) != 0)
        fail("file writer commit");
    file_request.flags = 0;
    file_request.buffer = (uint32_t)(uintptr_t)syscall_data;
    file_request.size = sizeof(syscall_data);
    if (call_sys(SS9_SYS_FILE_LOAD, (uint32_t)(uintptr_t)&file_request) != 4 ||
        syscall_data[0] != 'x' || syscall_data[1] != 'y' ||
        syscall_data[2] != 'x' || syscall_data[3] != 'y')
        fail("file writer content");
    file_request.flags = 2;
    if (call_sys(SS9_SYS_FILE_STORE, (uint32_t)(uintptr_t)&file_request) !=
        SS9_ERR_PARAM)
        fail("FAT16 syscall flags");
    file_request.flags = 0;
    file_request.buffer = 0;
    if (call_sys(SS9_SYS_FILE_LOAD, (uint32_t)(uintptr_t)&file_request) !=
        SS9_ERR_PARAM)
        fail("FAT16 syscall buffer");

    SSModuleId edit_module;
    if (ss_module_register_image(
            ss_test_edit_image,
            (uint32_t)(ss_test_edit_image_end - ss_test_edit_image),
            SS_MODULE_STORAGE_DYNAMIC, &edit_module) != SS_OK)
        fail("register editor image");
    const char* edit_argv[] = {"edit", "SOURCE.S"};
    SSProcessId edit_pid;
    if (ss_process_spawn(1, edit_module, 2, edit_argv, &edit_pid) != SS_OK)
        fail("spawn editor");
    ss_cli_set_foreground_pid(edit_pid);
    const uint8_t edit_keys[] = {'n', 'i', 'A', 27, ':', 'w', 'q', '\r'};
    for (unsigned i = 0; i < sizeof(edit_keys); i++) push_key(edit_keys[i]);
    int16_t edit_wait = SS_ERR_STATE;
    for (unsigned i = 0; i < 200 && edit_wait == SS_ERR_STATE; i++) {
        tick();
        edit_wait = ss_process_wait(1, edit_pid, &status);
    }
    ss_cli_set_foreground_pid(0);
    uint8_t edited[4] = {0};
    if (edit_wait != SS_OK || status != 0 ||
        ss_system_file_load("SOURCE.S", edited, sizeof(edited)) != 1 ||
        edited[0] != 'A')
        fail("editor create and save");
    char mux_command[] = "mux";
    if (ss_cli_execute_line(mux_command) != 0)
        fail("mux command");
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
    if (ss_process_spawn(1, edit_module, 2, edit_argv, &edit_pid) != SS_OK)
        fail("spawn editor for mux");
    ss_cli_set_foreground_pid(edit_pid);
    push_key('i');
    push_key('B');
    push_key(27);
    run_ticks(80);
    push_key('~');
    push_key('2');
    run_ticks(20);
    if (ss_cli_console_mux_active() != 1)
        fail("editor key switch away");
    run_ticks(80);
    push_key('~');
    push_key('1');
    run_ticks(20);
    if (ss_cli_console_mux_active() != 0)
        fail("editor key switch back");
    push_key(':');
    push_key('w');
    push_key('q');
    push_key('\r');
    edit_wait = SS_ERR_STATE;
    for (unsigned i = 0; i < 200 && edit_wait == SS_ERR_STATE; i++) {
        tick();
        edit_wait = ss_process_wait(1, edit_pid, &status);
    }
    ss_cli_set_foreground_pid(0);
    if (edit_wait != SS_OK || status != 0 ||
        ss_system_file_load("SOURCE.S", edited, sizeof(edited)) != 2 ||
        edited[0] != 'B' || edited[1] != 'A')
        fail("editor mux edit and replace");
    if (ss_module_unload(edit_module) != SS_OK)
        fail("unload editor");

    /* On-target assembler: store a source on FAT16, run the real S9M1 asm
     * module, and compare its object byte-for-byte with the host build of
     * the same core. */
    ss_test_console_count = 0; /* make room for the assembler's output */
    char asm_source_name[] = "PROG.S";
    SS9FileRequest asm_store __attribute__((aligned(4))) = {
        (uint32_t)(uintptr_t)asm_source_name,
        (uint32_t)(uintptr_t)ss_test_asm_source,
        (uint32_t)(ss_test_asm_source_end - ss_test_asm_source),
        SS9_FILE_CREATE};
    if (call_sys(SS9_SYS_FILE_STORE, (uint32_t)(uintptr_t)&asm_store) !=
        (int32_t)asm_store.size)
        fail("assembler source store");
    SSModuleId asm_module;
    if (ss_module_register_image(
            ss_test_asm_image,
            (uint32_t)(ss_test_asm_image_end - ss_test_asm_image),
            SS_MODULE_STORAGE_DYNAMIC, &asm_module) != SS_OK)
        fail("register assembler image");
    const char* asm_argv[] = {"asm", "PROG.S"};
    SSProcessId asm_pid;
    if (ss_process_spawn(1, asm_module, 2, asm_argv, &asm_pid) != SS_OK)
        fail("spawn assembler");
    ss_cli_set_foreground_pid(asm_pid);
    int16_t asm_wait = SS_ERR_STATE;
    for (unsigned i = 0; i < 5000 && asm_wait == SS_ERR_STATE; i++) {
        tick();
        asm_wait = ss_process_wait(1, asm_pid, &status);
    }
    ss_cli_set_foreground_pid(0);
    uint32_t expected_size =
        (uint32_t)(ss_test_asm_expected_end - ss_test_asm_expected);
    static uint8_t produced[512];
    if (asm_wait != SS_OK || status != 0 || !console_contains("Wrote PROG.O"))
        fail("assembler run");
    if (ss_system_file_load("PROG.O", produced, sizeof(produced)) !=
            (int32_t)expected_size ||
        expected_size > sizeof(produced))
        fail("assembler object size");
    for (uint32_t i = 0; i < expected_size; i++)
        if (produced[i] != ss_test_asm_expected[i])
            fail("assembler object bytes");
    if (ss_module_unload(asm_module) != SS_OK)
        fail("unload assembler");
    if (ss_process_kill(shells[0]) != SS_OK ||
        ss_process_kill(shells[1]) != SS_OK)
        fail("mux shell kill");
    uint16_t shell_status;
    int16_t first_wait = SS_ERR_STATE, second_wait = SS_ERR_STATE;
    for (unsigned i = 0;
         i < 200 && (first_wait == SS_ERR_STATE || second_wait == SS_ERR_STATE);
         i++) {
        tick();
        if (first_wait == SS_ERR_STATE)
            first_wait = ss_process_wait(1, shells[0], &shell_status);
        if (second_wait == SS_ERR_STATE)
            second_wait = ss_process_wait(1, shells[1], &shell_status);
    }
    if (first_wait != SS_OK || second_wait != SS_OK)
        fail("mux shell reap");
    tty_puts("OK shared code/private A5 data and hello\n");
    for (;;) tick();
}
