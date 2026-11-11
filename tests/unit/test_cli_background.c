#include "../../ssos/os/app/cli.h"
#include "../../ssos/os/app/cli_console.h"
#include "../../ssos/os/app/cli_wait.h"
#include "../../ssos/os/kernel/main_task.h"
#include "../../ssos/os/mem/system_arena.h"
#include "../../ssos/os/module/builtin.h"
#include "../../ssos/os/process/process.h"
#include "../../ssos/os/fs/scsi_volume.h"
#include "ssos_test.h"

static SSTask root_task;
static char output[8192];
static unsigned keyboard_polls;
static uint16_t keyboard_bytes[16];
static unsigned keyboard_length, keyboard_index;
static int interrupt_print;
static int16_t print_kill_status;
static uint8_t scsi_test_disk[5000][512];
static int enable_scsi_test;
static uint32_t captured_scsi_start;
static uint32_t captured_scsi_blocks;
static int test_scsi_read(void* context, uint32_t sector, uint8_t* out) {
    (void)context;
    if (sector >= 5000) return -1;
    memcpy(out, scsi_test_disk[sector], 512);
    return 0;
}
static int test_scsi_write(void* context, uint32_t sector, const uint8_t* in) {
    (void)context;
    if (sector >= 5000) return -1;
    memcpy(scsi_test_disk[sector], in, 512);
    return 0;
}

int ss_scsi_volume_mount_iocs(SSScsiVolume* volume, uint8_t target,
                              uint32_t start, uint32_t blocks) {
    if (!enable_scsi_test || target != 3) return -1;
    captured_scsi_start = start;
    captured_scsi_blocks = blocks;
    return ss_storage_mount(&volume->storage, test_scsi_read, test_scsi_write,
                            NULL, 5000, 512);
}

int _iocs_b_read(int mode, int position, int size, void* out) {
    (void)mode; (void)position; (void)size; (void)out;
    return -1;
}

int _iocs_b_putc(int ch) {
    size_t used = strlen(output);
    if (ch != 0 && used + 1u < sizeof(output)) {
        output[used] = (char)ch;
        output[used + 1u] = '\0';
    }
    return ch;
}

void _iocs_b_print(const char* text) {
    size_t room = sizeof(output) - strlen(output) - 1u;
    strncat(output, text, room);
    if (interrupt_print) {
        interrupt_print = 0;
        print_kill_status = ss_process_kill(ss_process_current_id());
    }
}
void _iocs_b_clr_st(void) {}
void _iocs_b_locate(int x, int y) {
    (void)x;
    (void)y;
}
int _iocs_b_keysns(void) {
    keyboard_polls++;
    return keyboard_index < keyboard_length;
}
int _iocs_b_keyinp(void) { return keyboard_bytes[keyboard_index++]; }

static void setup_shell(void) {
    ss_sched_init();
    (void)ss_memory_init();
    ss_module_registry_init();
    (void)ss_builtin_modules_init();
    (void)ss_main_task_register(&root_task, 8);
    (void)ss_process_init(&root_task);
    output[0] = '\0';
    keyboard_polls = 0;
    keyboard_length = keyboard_index = 0;
    interrupt_print = 0;
}

static void collect_child(SSProcessId pid) {
    SSProcessSnapshot child;
    if (ss_process_snapshot(pid, &child) != SS_OK)
        return;
    ss_curr_task = &root_task;
    (void)ss_process_kill(pid);
    ss_process_reap_task(child.task_id);
    (void)ss_task_release_terminated(child.task_id);
}

/* Execute the production C entry on the host, without claiming a 68000
 * stack/context switch. Completion is then reaped from the root's stack. */
static void execute_child(SSProcessId pid) {
    SSProcessSnapshot child;
    if (ss_process_snapshot(pid, &child) != SS_OK)
        return;
    SSTask* task = &tcb_table[child.task_id - 1];
    ss_curr_task = task;
    (void)task->entry(task->arg);
    collect_child(pid);
}

TEST(cli_background_builtin_exit_isolated) {
    setup_shell();
    uint32_t baseline = ss_memory_free_bytes();
    char line[] = "exit&";
    ASSERT_EQ(ss_cli_execute_line(line), 0);
    ASSERT_NOT_NULL(strstr(output, "Started command as PID 2"));
    SSProcessSnapshot child;
    ASSERT_EQ(ss_process_snapshot(2, &child), SS_OK);
    ASSERT_EQ(child.ppid, 1);
    ASSERT_EQ(child.module_id, 0);
    ASSERT_NOT_NULL(child.stack_low);
    ASSERT_STR_EQ((char*)child.data_base, "exit");
    line[0] = 'x';
    ASSERT_STR_EQ((char*)child.data_base, "exit");
    execute_child(2);
    uint16_t status = 99;
    ASSERT_EQ(ss_process_wait(1, 2, &status), SS_OK);
    ASSERT_EQ(status, 0);
    ASSERT_EQ(ss_process_snapshot(1, &child), SS_OK);
    ASSERT_EQ(child.state, SS_PROCESS_READY);
    ASSERT_EQ(ss_memory_free_bytes(), baseline);
}

TEST(process_console_inherited) {
    setup_shell();
    ASSERT_EQ(ss_process_current_console(), 0);
    ASSERT_EQ(ss_process_set_current_console(2), SS_OK);
    ASSERT_EQ(ss_process_set_current_console(3), SS_ERR_PARAM);
    char line[] = "help&";
    ASSERT_EQ(ss_cli_execute_line(line), 0);
    SSProcessSnapshot child;
    ASSERT_EQ(ss_process_snapshot(2, &child), SS_OK);
    ASSERT_EQ(child.console_id, 2);
    ss_curr_task = &tcb_table[child.task_id - 1];
    ASSERT_EQ(ss_process_current_console(), 2);
    ASSERT_EQ(ss_process_set_current_console(1), SS_OK);
    ss_curr_task = &root_task;
    ASSERT_EQ(ss_process_current_console(), 2);
    collect_child(2);
    uint16_t status;
    ASSERT_EQ(ss_process_wait(1, 2, &status), SS_OK);
}

TEST(console_mux_routes_output) {
    setup_shell();
    ss_cli_console_mux_enable();
    output[0] = '\0';
    ss_cli_console_print("visible");
    ASSERT_STR_EQ(output, "visible");
    ASSERT_EQ(ss_process_set_current_console(2), SS_OK);
    ss_cli_console_print("hidden");
    ASSERT_STR_EQ(output, "visible");
    output[0] = '\0';
    ASSERT_EQ(ss_cli_console_mux_select(2), 0);
    ASSERT_STR_EQ(output, "hidden");
    output[0] = '\0';
    ss_cli_console_write((const uint8_t*)"!", 1);
    ASSERT_STR_EQ(output, "!");
    keyboard_bytes[0] = 'x'; keyboard_bytes[1] = 0x1d;
    keyboard_bytes[2] = '1'; keyboard_bytes[3] = 'y';
    keyboard_length = 4;
    ASSERT_EQ(ss_cli_console_read_key(), 'x');
    ASSERT_EQ(ss_cli_console_mux_active(), 0);
    ASSERT_EQ(ss_process_set_current_console(0), SS_OK);
    ASSERT_EQ(ss_cli_console_read_key(), 'y');
    ASSERT_EQ(ss_cli_console_read_key(), -1);
    keyboard_index = 0;
    keyboard_length = 1;
    keyboard_bytes[0] = 0x6400;
    ASSERT_EQ(ss_cli_console_read_key(), -1);
    ASSERT_EQ(ss_cli_console_mux_active(), 1);
    keyboard_index = 0;
    keyboard_bytes[0] = 0x6500;
    ASSERT_EQ(ss_cli_console_read_key(), -1);
    ASSERT_EQ(ss_cli_console_mux_active(), 2);
    keyboard_index = 0;
    keyboard_bytes[0] = 0x6300;
    ASSERT_EQ(ss_cli_console_read_key(), -1);
    ASSERT_EQ(ss_cli_console_mux_active(), 0);
    ASSERT_EQ(ss_cli_console_mux_select(0), 0);
    keyboard_index = 0;
    keyboard_length = 4;
    keyboard_bytes[0] = '~'; keyboard_bytes[1] = '3';
    keyboard_bytes[2] = '~'; keyboard_bytes[3] = '~';
    ASSERT_EQ(ss_cli_console_read_key(), -1);
    ASSERT_EQ(ss_cli_console_mux_active(), 2);
    ASSERT_EQ(ss_process_set_current_console(2), SS_OK);
    ASSERT_EQ(ss_cli_console_read_key(), '~');
    ASSERT_EQ(ss_process_set_current_console(0), SS_OK);
    ASSERT_EQ(ss_cli_console_mux_select(0), 0);
    ss_ctrl_c_pending = 1;
    ASSERT_EQ(ss_process_set_current_console(2), SS_OK);
    ASSERT_EQ(ss_cli_poll_ctrl_c(), 0);
    ASSERT_EQ(ss_process_set_current_console(0), SS_OK);
    ASSERT_EQ(ss_cli_poll_ctrl_c(), 1);
}

TEST(cli_scsi_mount_write_read) {
    setup_shell();
    memset(scsi_test_disk, 0, sizeof(scsi_test_disk));
    uint8_t* boot = scsi_test_disk[0];
    boot[12] = 2; boot[13] = 1; boot[14] = 1; boot[16] = 2;
    boot[17] = 16; boot[19] = 0x88; boot[20] = 0x13; boot[22] = 20;
    boot[510] = 0x55; boot[511] = 0xaa;
    enable_scsi_test = 1;
    char invalid[] = "smount 3 123 0";
    ASSERT_EQ(ss_cli_execute_line(invalid), 0);
    ASSERT_NOT_NULL(strstr(output, "Invalid SCSI target"));
    output[0] = '\0';
    char mount[] = "smount 3 123 5000";
    ASSERT_EQ(ss_cli_execute_line(mount), 0);
    ASSERT_EQ(captured_scsi_start, 123);
    ASSERT_EQ(captured_scsi_blocks, 5000);
    ASSERT_NOT_NULL(strstr(output, "FAT16 mounted."));
    char write[] = "swrite TEST.TXT hello";
    output[0] = '\0';
    ASSERT_EQ(ss_cli_execute_line(write), 0);
    ASSERT_NOT_NULL(strstr(output, "File written."));
    char replace[] = "sreplace TEST.TXT world";
    ASSERT_EQ(ss_cli_execute_line(replace), 0);
    output[0] = '\0';
    char read[] = "sread TEST.TXT";
    ASSERT_EQ(ss_cli_execute_line(read), 0);
    ASSERT_STR_EQ(output, "world\r\n");
    enable_scsi_test = 0;
}

TEST(mux_starts_shell_processes) {
    setup_shell();
    char line[] = "mux";
    ASSERT_EQ(ss_cli_execute_line(line), 0);
    SSProcessSnapshot first, second;
    ASSERT_EQ(ss_process_snapshot(2, &first), SS_OK);
    ASSERT_EQ(ss_process_snapshot(3, &second), SS_OK);
    ASSERT_EQ(first.ppid, 1);
    ASSERT_EQ(second.ppid, 1);
    ASSERT_EQ(*(uint8_t*)first.data_base, 1);
    ASSERT_EQ(*(uint8_t*)second.data_base, 2);
    ASSERT_EQ(ss_cli_console_mux_select(1), 0);
    for (unsigned i = 0; i < 5; i++) keyboard_bytes[i] = "exit\r"[i];
    keyboard_length = 5;
    SSTask* task = &tcb_table[first.task_id - 1];
    ss_curr_task = task;
    (void)task->entry(task->arg);
    ASSERT_EQ(ss_process_current_console(), 1);
    collect_child(2);
    uint16_t status;
    ASSERT_EQ(ss_process_wait(1, 2, &status), SS_OK);
    collect_child(3);
    ASSERT_EQ(ss_process_wait(1, 3, &status), SS_OK);
}

TEST(cli_background_wait_cannot_reap_parent_child) {
    setup_shell();
    char counter[] = "run counter&";
    ASSERT_EQ(ss_cli_execute_line(counter), 0);
    char wait_line[] = "wait 2&";
    ASSERT_EQ(ss_cli_execute_line(wait_line), 0);
    execute_child(3);
    uint16_t status;
    ASSERT_EQ(ss_process_wait(1, 3, &status), SS_OK);
    ASSERT_EQ(status, 127);
    ASSERT_EQ(keyboard_polls, 0);
    SSProcessSnapshot child;
    ASSERT_EQ(ss_process_snapshot(2, &child), SS_OK);
    ASSERT_EQ(child.ppid, 1);
    ASSERT_EQ(child.state, SS_PROCESS_READY);
    collect_child(2);
    ASSERT_EQ(ss_process_wait(1, 2, &status), SS_OK);
}

TEST(cli_background_diagnostics_and_kill) {
    setup_shell();
    uint32_t baseline = ss_memory_free_bytes();
    const char* commands[] = {"help&",   "ps &",   "modules&", "mem&",
                              "uptime&", "trace&", "version&", "clear&"};
    for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        char line[64];
        strcpy(line, commands[i]);
        ASSERT_EQ(ss_cli_execute_line(line), 0);
        SSProcessId pid = i + 2u;
        execute_child(pid);
        uint16_t status;
        ASSERT_EQ(ss_process_wait(1, pid, &status), SS_OK);
        ASSERT_EQ(status, 0);
        ASSERT_EQ(ss_memory_free_bytes(), baseline);
    }
    ASSERT_NOT_NULL(strstr(output, "arena free_bytes="));
    ASSERT_NOT_NULL(strstr(output, "counter builtin"));
    char counter[] = "run counter &";
    ASSERT_EQ(ss_cli_execute_line(counter), 0);
    char kill_line[] = "kill 10&";
    ASSERT_EQ(ss_cli_execute_line(kill_line), 0);
    execute_child(11);
    uint16_t status;
    ASSERT_EQ(ss_process_wait(1, 11, &status), SS_OK);
    SSProcessSnapshot child;
    ASSERT_EQ(ss_process_snapshot(10, &child), SS_OK);
    ASSERT_EQ(tcb_table[child.task_id - 1].state, SS_TS_TERMINATED);
    collect_child(10);
    ASSERT_EQ(ss_process_wait(1, 10, &status), SS_OK);
    ASSERT_EQ(ss_memory_free_bytes(), baseline);
}

TEST(cli_kernel_child_slot_failure_rolls_back) {
    setup_shell();
    SSProcessId children[SS_PROCESS_MAX - 1];
    for (unsigned i = 0; i < SS_PROCESS_MAX - 1; i++) {
        char line[] = "help&";
        ASSERT_EQ(ss_cli_execute_line(line), 0);
        children[i] = i + 2;
    }
    uint32_t exhausted = ss_memory_free_bytes();
    char extra[] = "help&";
    ASSERT_EQ(ss_cli_execute_line(extra), 0);
    ASSERT_NOT_NULL(strstr(output, "Unable to start background command."));
    ASSERT_EQ(ss_memory_free_bytes(), exhausted);
    for (unsigned i = 0; i < SS_PROCESS_MAX - 1; i++) {
        collect_child(children[i]);
        uint16_t status;
        ASSERT_EQ(ss_process_wait(1, children[i], &status), SS_OK);
    }
}

TEST(cli_invalid_ampersand_has_no_side_effect) {
    setup_shell();
    uint32_t baseline = ss_memory_free_bytes();
    char line[] = "run counter&&";
    ASSERT_EQ(ss_cli_execute_line(line), 0);
    SSProcessSnapshot child;
    ASSERT_EQ(ss_process_snapshot(2, &child), SS_ERR_ID);
    ASSERT_EQ(ss_memory_free_bytes(), baseline);
}

TEST(cli_background_cancel_during_output_releases_console) {
    setup_shell();
    uint32_t baseline = ss_memory_free_bytes();
    char line[] = "help&";
    ASSERT_EQ(ss_cli_execute_line(line), 0);
    interrupt_print = 1;
    execute_child(2);
    ASSERT_EQ(print_kill_status, SS_OK);
    uint16_t status;
    ASSERT_EQ(ss_process_wait(1, 2, &status), SS_OK);
    ASSERT_EQ(status, 130);
    char foreground[] = "version";
    ASSERT_EQ(ss_cli_execute_line(foreground), 0);
    ASSERT_NOT_NULL(strstr(output, "SSOS-9 CLI preemptive minimum"));
    ASSERT_EQ(ss_memory_free_bytes(), baseline);
}

static void* dummy_entry(void* argument) { return argument; }

TEST(cli_kernel_child_allocation_and_task_failure_roll_back) {
    setup_shell();
    uint32_t baseline = ss_memory_free_bytes();
    void* exhausted = ss_memory_alloc(ss_memory_largest_free());
    ASSERT_NOT_NULL(exhausted);
    char line[] = "help&";
    ASSERT_EQ(ss_cli_execute_line(line), 0);
    SSProcessSnapshot child;
    ASSERT_EQ(ss_process_snapshot(2, &child), SS_ERR_ID);
    ASSERT_EQ(ss_memory_free(exhausted), SS_OK);
    ASSERT_EQ(ss_memory_free_bytes(), baseline);
    uint16_t tasks[SS_MAX_TASKS];
    SSTaskInfo info = {dummy_entry,   8,    SS_CTX_NORMAL,
                       SS_TASK_STACK, NULL, NULL};
    for (unsigned i = 0; i < SS_MAX_TASKS; i++) {
        tasks[i] = ss_task_create(&info);
        ASSERT_IN_RANGE(tasks[i], 1, SS_MAX_TASKS);
    }
    char task_failure[] = "help&";
    ASSERT_EQ(ss_cli_execute_line(task_failure), 0);
    ASSERT_EQ(ss_process_snapshot(3, &child), SS_ERR_ID);
    ASSERT_EQ(ss_memory_free_bytes(), baseline);
    for (unsigned i = 0; i < SS_MAX_TASKS; i++) {
        ASSERT_EQ(ss_task_cancel(tasks[i]), SS_OK);
        ASSERT_EQ(ss_task_release_terminated(tasks[i]), SS_OK);
    }
    char retry[] = "help&";
    ASSERT_EQ(ss_cli_execute_line(retry), 0);
    ASSERT_EQ(ss_process_snapshot(4, &child), SS_OK);
    execute_child(4);
    uint16_t status;
    ASSERT_EQ(ss_process_wait(1, 4, &status), SS_OK);
    ASSERT_EQ(ss_memory_free_bytes(), baseline);
}

TEST(cli_background_builtin_error_status) {
    setup_shell();
    const char* commands[] = {"kill 999&", "help extra&", "missing&"};
    const uint16_t expected[] = {1, 2, 127};
    for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        char line[64];
        strcpy(line, commands[i]);
        ASSERT_EQ(ss_cli_execute_line(line), 0);
        execute_child(i + 2u);
        uint16_t status;
        ASSERT_EQ(ss_process_wait(1, i + 2u, &status), SS_OK);
        ASSERT_EQ(status, expected[i]);
    }
}

static unsigned reap_calls;
static uint16_t nested_release;
static uint8_t claimed_state;

static void checked_reaper(uint16_t id) {
    reap_calls++;
    claimed_state = tcb_table[id - 1].state;
    nested_release = ss_task_release_terminated(id);
    ss_process_reap_task(id);
}

TEST(process_reaper_claims_before_hook_and_release) {
    setup_shell();
    uint32_t baseline = ss_memory_free_bytes();
    char line[] = "help&";
    ASSERT_EQ(ss_cli_execute_line(line), 0);
    SSProcessSnapshot child;
    ASSERT_EQ(ss_process_snapshot(2, &child), SS_OK);
    reap_calls = 0;
    ss_reaper_set_hook(checked_reaper);
    /* Simulate the old race: the reaper visits a live task, then another
     * task kills it before the next reaper operation. */
    ASSERT_EQ((int16_t)ss_task_release_terminated(child.task_id), SS_ERR_STATE);
    ASSERT_EQ(reap_calls, 0);
    ASSERT_EQ(ss_process_kill(2), SS_OK);
    ASSERT_EQ(ss_task_release_terminated(child.task_id), SS_OK);
    ASSERT_EQ(reap_calls, 1);
    ASSERT_EQ(claimed_state, SS_TS_REAPING);
    ASSERT_EQ((int16_t)nested_release, SS_ERR_STATE);
    ASSERT_EQ(tcb_table[child.task_id - 1].state, SS_TS_NONE);
    uint16_t status;
    ASSERT_EQ(ss_process_wait(1, 2, &status), SS_OK);
    ASSERT_EQ(ss_memory_free_bytes(), baseline);
    ss_reaper_set_hook(ss_process_reap_task);
}

TEST(process_syscall_memory_ownership) {
    setup_shell();
    SSModuleId module;
    ASSERT_EQ(ss_module_find("counter", &module), SS_OK);
    SSProcessId pid;
    ASSERT_EQ(ss_process_spawn(1, module, 0, NULL, &pid), SS_OK);
    SSProcessSnapshot child;
    SSModuleSnapshot image;
    ASSERT_EQ(ss_process_snapshot(pid, &child), SS_OK);
    ASSERT_EQ(ss_module_snapshot(module, &image), SS_OK);
    ss_curr_task = &tcb_table[child.task_id - 1];
    ASSERT_TRUE(ss_process_memory_access((uintptr_t)child.data_base, 1, 1));
    ASSERT_TRUE(
        ss_process_memory_access((uintptr_t)child.stack_high - 4, 4, 1));
    ASSERT_TRUE(
        !ss_process_memory_access((uintptr_t)child.stack_high - 4, 8, 1));
    ASSERT_TRUE(
        !ss_process_memory_access((uintptr_t)child.data_base - 1, 1, 0));
    ASSERT_TRUE(ss_process_memory_access((uintptr_t)image.image.image,
                                         image.image.image_size, 0));
    ASSERT_TRUE(!ss_process_memory_access((uintptr_t)image.image.image, 1, 1));
    ASSERT_TRUE(!ss_process_memory_access((uintptr_t)&root_task, 4, 1));
    ASSERT_TRUE(!ss_process_memory_access(UINTPTR_MAX - 3u, 8, 0));
    ASSERT_TRUE(!ss_process_memory_access(0, 4, 0));
    ss_curr_task = &root_task;
    collect_child(pid);
    uint16_t status;
    ASSERT_EQ(ss_process_wait(1, pid, &status), SS_OK);
}

void run_cli_background_tests(void) {
    RUN_TEST(process_reaper_claims_before_hook_and_release);
    RUN_TEST(process_syscall_memory_ownership);
    RUN_TEST(process_console_inherited);
    RUN_TEST(cli_background_builtin_exit_isolated);
    RUN_TEST(cli_background_wait_cannot_reap_parent_child);
    RUN_TEST(cli_background_diagnostics_and_kill);
    RUN_TEST(cli_kernel_child_slot_failure_rolls_back);
    RUN_TEST(cli_invalid_ampersand_has_no_side_effect);
    RUN_TEST(cli_background_builtin_error_status);
    RUN_TEST(cli_background_cancel_during_output_releases_console);
    RUN_TEST(cli_kernel_child_allocation_and_task_failure_roll_back);
    RUN_TEST(cli_scsi_mount_write_read);
    RUN_TEST(console_mux_routes_output);
    RUN_TEST(mux_starts_shell_processes);
}
