#include "../../ssos/os/app/cli.h"
#include "../../ssos/os/app/cli_console.h"
#include "../../ssos/os/app/cli_wait.h"
#include "../../ssos/os/kernel/main_task.h"
#include "../../ssos/os/mem/system_arena.h"
#include "../../ssos/os/module/builtin.h"
#include "../../ssos/os/process/process.h"
#include "../../ssos/os/fs/scsi_volume.h"
#include "human68k_disk.h"
#include "../../ssos/include/ssos9/syscall.h"
#include "ssos_test.h"

static SSTask root_task;
static char output[8192];
static unsigned keyboard_polls;
static uint16_t keyboard_bytes[16];
static unsigned keyboard_length, keyboard_index;
static int interrupt_print;
static uint8_t display_cells[32][80];
static unsigned display_x, display_y;
static int16_t print_kill_status;
static uint8_t hd_store[HD_STORE_BYTES(HD_STORE_MIN + 40u)];
static int enable_scsi_test;
static uint32_t captured_scsi_start;
static uint32_t captured_scsi_blocks;

int ss_scsi_volume_mount_iocs(SSScsiVolume* volume, uint8_t target,
                              uint32_t start, uint32_t blocks) {
    if (!enable_scsi_test || target != 3) return -1;
    captured_scsi_start = start;
    captured_scsi_blocks = blocks;
    return hd_mount(volume, target, start, blocks);
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
    if (ch == '\r') display_x = 0;
    else if (ch == '\n') { if (display_y < 31) display_y++; }
    else if (ch >= 32 && ch < 127) {
        display_cells[display_y][display_x] = (uint8_t)ch;
        if (++display_x == 80) { display_x = 0; if (display_y < 31) display_y++; }
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
void _iocs_b_clr_st(void) {
    memset(display_cells, ' ', sizeof(display_cells));
    display_x = display_y = 0;
}
void _iocs_b_locate(int x, int y) {
    display_x = (unsigned)x;
    display_y = (unsigned)y;
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
    /* Terminate through the task layer only: on the target a finished task
     * reaches the reaper via ss_task_exit, not via ss_process_kill, so this
     * helper must not stamp the kill exit status (R-05). */
    (void)ss_task_cancel(child.task_id);
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

TEST(cli_run_passes_arguments) {
    setup_shell();
    char line[] = "run counter SOURCE.S OTHER.O&";
    ASSERT_EQ(ss_cli_execute_line(line), 0);
    SSProcessSnapshot child;
    ASSERT_EQ(ss_process_snapshot(2, &child), SS_OK);
    memset(line, 'x', sizeof(line) - 1u);
    const uint8_t* data = child.data_base;
    size_t size = (size_t)((const uint8_t*)child.stack_low - data);
    int found = 0;
    static const char args[] = "counter\0SOURCE.S\0OTHER.O";
    for (size_t i = 0; i + sizeof(args) <= size; i++)
        if (memcmp(data + i, args, sizeof(args)) == 0) found = 1;
    ASSERT_TRUE(found);
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
    keyboard_bytes[0] = 'x'; keyboard_bytes[1] = 0x6300;
    keyboard_bytes[2] = '1'; keyboard_bytes[3] = 'y';
    keyboard_length = 4;
    ASSERT_EQ(ss_cli_console_read_key(), 'x');
    ASSERT_EQ(ss_cli_console_mux_active(), 0);
    ASSERT_EQ(ss_process_set_current_console(0), SS_OK);
    ASSERT_EQ(ss_cli_console_read_key(), '1');
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
    keyboard_bytes[2] = 0x1d; keyboard_bytes[3] = '2';
    ASSERT_EQ(ss_cli_console_read_key(), '~');
    ASSERT_EQ(ss_cli_console_read_key(), '3');
    ASSERT_EQ(ss_cli_console_read_key(), 0x1d);
    ASSERT_EQ(ss_cli_console_read_key(), '2');
    ASSERT_EQ(ss_cli_console_mux_active(), 0);
    keyboard_index = 0;
    keyboard_length = 2;
    keyboard_bytes[0] = '~';
    keyboard_bytes[1] = 0x3b00;
    ASSERT_EQ(ss_cli_console_read_key(), '~');
    ASSERT_EQ(ss_cli_console_read_key(), 0x3b00);
    ss_ctrl_c_pending = 1;
    ASSERT_EQ(ss_process_set_current_console(2), SS_OK);
    ASSERT_EQ(ss_cli_poll_ctrl_c(), 0);
    ASSERT_EQ(ss_process_set_current_console(0), SS_OK);
    ASSERT_EQ(ss_cli_poll_ctrl_c(), 1);
}

TEST(console_foreground_input_routing) {
    setup_shell();
    char line[] = "help&";
    ASSERT_EQ(ss_cli_execute_line(line), 0);
    SSProcessSnapshot child;
    ASSERT_EQ(ss_process_snapshot(2, &child), SS_OK);
    ss_cli_set_foreground_pid(2);
    keyboard_bytes[0] = 'q';
    keyboard_bytes[1] = 0x3b00;
    keyboard_length = 2;
    ASSERT_EQ(ss_cli_foreground_key(), -1);
    ASSERT_EQ(keyboard_index, 0);
    ASSERT_EQ(ss_cli_poll_ctrl_c(), 0);
    ss_curr_task = &tcb_table[child.task_id - 1];
    ASSERT_EQ(ss_cli_foreground_key(), 'q');
    ASSERT_EQ(ss_cli_foreground_key(), 0x3b00);
    keyboard_index = 0;
    keyboard_length = 1;
    keyboard_bytes[0] = 3;
    ASSERT_EQ(ss_cli_foreground_key(), -1);
    ss_curr_task = &root_task;
    ASSERT_EQ(ss_cli_poll_ctrl_c(), 1);
    ss_cli_set_foreground_pid(0);
    collect_child(2);
    uint16_t status;
    ASSERT_EQ(ss_process_wait(1, 2, &status), SS_OK);
}

TEST(console_scroll_matches_virtual_screen) {
    setup_shell();
    ss_cli_console_mux_enable();
    for (unsigned i = 0; i < 30; i++) {
        uint8_t line[] = {'0', '0', '\r', '\n'};
        line[0] = (uint8_t)('0' + i / 10);
        line[1] = (uint8_t)('0' + i % 10);
        ss_cli_console_write(line, sizeof(line));
    }
    ASSERT_EQ(memcmp(display_cells[0], "06", 2), 0);
    ASSERT_EQ(memcmp(display_cells[23], "29", 2), 0);
    ASSERT_EQ(display_cells[24][0], ' ');
    ASSERT_EQ(ss_cli_console_mux_select(1), 0);
    ASSERT_EQ(ss_cli_console_mux_select(0), 0);
    ASSERT_EQ(memcmp(display_cells[0], "06", 2), 0);
    ASSERT_EQ(memcmp(display_cells[23], "29", 2), 0);
    ss_cli_console_print("30\r\n");
    ASSERT_EQ(memcmp(display_cells[0], "07", 2), 0);
    ASSERT_EQ(memcmp(display_cells[23], "30", 2), 0);
}

TEST(console_background_scroll_preserves_active_window) {
    setup_shell();
    ss_cli_console_mux_enable();
    ss_cli_console_write((const uint8_t*)"active", 6);
    ASSERT_EQ(ss_process_set_current_console(1), SS_OK);
    for (unsigned i = 0; i < 30; i++) {
        uint8_t line[] = {'0', '0', '\r', '\n'};
        line[0] = (uint8_t)('0' + i / 10);
        line[1] = (uint8_t)('0' + i % 10);
        ss_cli_console_write(line, sizeof(line));
    }
    ASSERT_EQ(memcmp(display_cells[0], "active", 6), 0);
    ASSERT_EQ(ss_cli_console_mux_select(1), 0);
    ASSERT_EQ(memcmp(display_cells[0], "06", 2), 0);
    ASSERT_EQ(memcmp(display_cells[23], "29", 2), 0);
    ASSERT_EQ(ss_cli_console_mux_select(0), 0);
    ASSERT_EQ(memcmp(display_cells[0], "active", 6), 0);
    ASSERT_EQ(ss_process_set_current_console(0), SS_OK);
}

TEST(cli_scsi_mount_write_read) {
    setup_shell();
    /* The window starts at sector 50 (block 100). */
    hd_format(hd_store, HD_STORE_MIN + 40u, 50);
    enable_scsi_test = 1;
    char invalid[] = "smount 3 123 0";
    ASSERT_EQ(ss_cli_execute_line(invalid), 0);
    ASSERT_NOT_NULL(strstr(output, "Invalid SCSI target"));
    output[0] = '\0';
    char narrow[] = "smount 3 100 5000";
    ASSERT_EQ(ss_cli_execute_line(narrow), 0);
    ASSERT_NOT_NULL(strstr(output, "Unable to mount FAT16."));
    output[0] = '\0';
    char misaligned[] = "smount 3 123";
    ASSERT_EQ(ss_cli_execute_line(misaligned), 0);
    ASSERT_NOT_NULL(strstr(output, "Unable to mount FAT16."));
    output[0] = '\0';
    char mount[] = "smount 3 100 18064";
    ASSERT_EQ(ss_cli_execute_line(mount), 0);
    ASSERT_EQ(captured_scsi_start, 100);
    ASSERT_EQ(captured_scsi_blocks, 18064);
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
    ASSERT_EQ(ss_system_open_reader(1, "TEST.TXT"), 3);
    ASSERT_EQ(ss_system_open_reader(2, "TEST.TXT"), 3);
    ASSERT_EQ(ss_system_open_reader(1, "TEST.TXT"), 4);
    uint8_t chunk[6] = {0};
    ASSERT_EQ(ss_system_read_fd(1, 3, chunk, 2), 2);
    ASSERT_EQ(ss_system_read_fd(2, 3, chunk + 2, 2), 2);
    ASSERT_EQ(memcmp(chunk, "wowo", 4), 0);
    ASSERT_EQ(ss_system_read_fd(3, 3, chunk, 1), -1);
    ASSERT_EQ(ss_system_file_store("TEST.TXT", (const uint8_t*)"x", 1, 1), -1);
    ss_system_close_process_files(2);
    ASSERT_EQ(ss_system_read_fd(2, 3, chunk, 1), -1);
    ASSERT_EQ(ss_system_read_fd(1, 3, chunk, 3), 3);
    ASSERT_EQ(memcmp(chunk, "rld", 3), 0);
    ASSERT_EQ(ss_system_read_fd(1, 3, chunk, 1), 0);
    ASSERT_EQ(ss_system_close_fd(1, 3), 0);
    ASSERT_EQ(ss_system_close_fd(1, 3), -1);
    ASSERT_EQ(ss_system_close_fd(1, 4), 0);
    ASSERT_EQ(ss_system_file_store("TEST.TXT", (const uint8_t*)"x", 1, 1), 1);
    char child_command[] = "help&";
    ASSERT_EQ(ss_cli_execute_line(child_command), 0);
    ASSERT_EQ(ss_system_open_reader(2, "TEST.TXT"), 3);
    collect_child(2);
    ASSERT_EQ(ss_system_read_fd(2, 3, chunk, 1), -1);
    ASSERT_EQ(ss_system_file_store("TEST.TXT", (const uint8_t*)"y", 1, 1), 1);
    for (int32_t fd = 3; fd < 11; fd++)
        ASSERT_EQ(ss_system_open_reader(1, "TEST.TXT"), fd);
    ASSERT_EQ(ss_system_open_reader(1, "TEST.TXT"), -1);
    for (int32_t fd = 3; fd < 11; fd++)
        ASSERT_EQ(ss_system_open_reader(2, "TEST.TXT"), fd);
    ASSERT_EQ(ss_system_open_reader(3, "TEST.TXT"), -1);
    ss_system_close_process_files(1);
    ss_system_close_process_files(2);
    ASSERT_EQ(ss_system_open_reader(3, "TEST.TXT"), 3);
    ss_system_close_process_files(3);
    ASSERT_EQ(ss_system_open_writer(1, "TEST.TXT", 1), 3);
    ASSERT_EQ(ss_system_open_reader(2, "TEST.TXT"), -1);
    ASSERT_EQ(ss_system_open_writer(2, "NEW.TXT", 0), -1);
    ASSERT_EQ(ss_system_write_fd(2, 3, (const uint8_t*)"bad", 3), -1);
    ASSERT_EQ(ss_system_write_fd(1, 3, (const uint8_t*)"ab", 2), 2);
    /* A whole-file load is rejected while a writer transaction is open;
     * the committed content is readable again after the close below. */
    ASSERT_EQ(ss_system_file_load("TEST.TXT", chunk, sizeof(chunk)), -1);
    ASSERT_EQ(ss_system_write_fd(1, 3, (const uint8_t*)"cd", 2), 2);
    ASSERT_EQ(ss_system_close_fd(1, 3), 0);
    ASSERT_EQ(ss_system_file_load("TEST.TXT", chunk, sizeof(chunk)), 4);
    ASSERT_EQ(memcmp(chunk, "abcd", 4), 0);
    ASSERT_EQ(ss_system_open_writer(2, "TEST.TXT", 1), 3);
    ASSERT_EQ(ss_system_write_fd(2, 3, (const uint8_t*)"zz", 2), 2);
    ss_system_close_process_files(2);
    ASSERT_EQ(ss_system_file_load("TEST.TXT", chunk, sizeof(chunk)), 4);
    ASSERT_EQ(memcmp(chunk, "abcd", 4), 0);
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

/* K-01: a mux subshell's finished background jobs are reparented to the
 * root on subshell death, and the root never waits for them. The orphan
 * sweep must reclaim exactly those zombie slots. */
TEST(orphan_zombie_is_reaped_by_orphan_sweep) {
    setup_shell();
    SSProcessId parent_pid, child_pid;
    ASSERT_EQ(ss_process_spawn_kernel(1, "parent", dummy_entry, "p", 1,
                                      &parent_pid), SS_OK);
    ASSERT_EQ(ss_process_spawn_kernel(parent_pid, "child", dummy_entry, "c",
                                      1, &child_pid), SS_OK);
    SSProcessSnapshot snapshot;
    ASSERT_EQ(ss_process_snapshot(child_pid, &snapshot), SS_OK);
    ASSERT_EQ(snapshot.ppid, parent_pid);

    /* The parent dies: the child is reparented to the root as an orphan. */
    collect_child(parent_pid);
    ASSERT_EQ(ss_process_snapshot(child_pid, &snapshot), SS_OK);
    ASSERT_EQ(snapshot.ppid, 1);
    ASSERT_EQ(snapshot.state, SS_PROCESS_READY);

    /* The orphan exits; nobody waits for it, so the slot must stay ZOMBIE
     * and out of reach for a non-parent wait. */
    execute_child(child_pid);
    ASSERT_EQ(ss_process_snapshot(child_pid, &snapshot), SS_OK);
    ASSERT_EQ(snapshot.state, SS_PROCESS_ZOMBIE);
    uint16_t status;
    ASSERT_EQ(ss_process_wait(child_pid, child_pid, &status), SS_ERR_ID);
    ASSERT_EQ(ss_process_snapshot(child_pid, &snapshot), SS_OK);
    ASSERT_EQ(snapshot.state, SS_PROCESS_ZOMBIE);

    /* The sweep frees the orphan slot, leaves the direct root child's
     * zombie untouched, and keeps the table walkable. */
    ss_process_reap_orphans();
    ASSERT_EQ(ss_process_snapshot(child_pid, &snapshot), SS_ERR_ID);
    ASSERT_EQ(ss_process_snapshot(parent_pid, &snapshot), SS_OK);
    ASSERT_EQ(snapshot.state, SS_PROCESS_ZOMBIE);
    unsigned live = 0;
    for (uint16_t i = 0; i < SS_PROCESS_MAX; i++)
        if (ss_process_snapshot_at(i, &snapshot) == SS_OK) live++;
    ASSERT_EQ(live, 2);
    ASSERT_EQ(ss_process_wait(1, parent_pid, &status), SS_OK);
}

TEST(normal_child_wait_result_untouched_by_orphan_sweep) {
    setup_shell();
    SSProcessId pid;
    ASSERT_EQ(ss_process_spawn_kernel(1, "normal", dummy_entry, "n", 1,
                                      &pid), SS_OK);
    execute_child(pid);
    SSProcessSnapshot snapshot;
    ASSERT_EQ(ss_process_snapshot(pid, &snapshot), SS_OK);
    ASSERT_EQ(snapshot.state, SS_PROCESS_ZOMBIE);
    ss_process_reap_orphans();
    ASSERT_EQ(ss_process_snapshot(pid, &snapshot), SS_OK);
    ASSERT_EQ(snapshot.state, SS_PROCESS_ZOMBIE);
    uint16_t status = 99;
    ASSERT_EQ(ss_process_wait(1, pid, &status), SS_OK);
    ASSERT_EQ(status, 0);
    ASSERT_EQ(ss_process_snapshot(pid, &snapshot), SS_ERR_ID);
}

TEST(orphan_cycles_keep_slot_count_stable) {
    setup_shell();
    SSProcessSnapshot snapshot;
    for (unsigned cycle = 0; cycle < 3 * SS_PROCESS_MAX; cycle++) {
        SSProcessId parent_pid, child_pid;
        ASSERT_EQ(ss_process_spawn_kernel(1, "parent", dummy_entry, "p", 1,
                                          &parent_pid), SS_OK);
        ASSERT_EQ(ss_process_spawn_kernel(parent_pid, "child", dummy_entry,
                                          "c", 1, &child_pid), SS_OK);
        collect_child(parent_pid);
        execute_child(child_pid);
        ss_process_reap_orphans();
        ASSERT_EQ(ss_process_snapshot(child_pid, &snapshot), SS_ERR_ID);
        uint16_t status;
        ASSERT_EQ(ss_process_wait(1, parent_pid, &status), SS_OK);
    }
    unsigned live = 0;
    for (uint16_t i = 0; i < SS_PROCESS_MAX; i++)
        if (ss_process_snapshot_at(i, &snapshot) == SS_OK) live++;
    ASSERT_EQ(live, 1);
}

/* R-05: a killed child must be distinguishable from exit(0) via wait. */
TEST(killed_child_wait_reports_kill_status) {
    setup_shell();
    SSProcessId pid;
    ASSERT_EQ(ss_process_spawn_kernel(1, "doomed", dummy_entry, "d", 1,
                                      &pid), SS_OK);
    ASSERT_EQ(ss_process_kill(pid), SS_OK);
    collect_child(pid);
    uint16_t status = 0;
    ASSERT_EQ(ss_process_wait(1, pid, &status), SS_OK);
    ASSERT_EQ(status, SS9_EXIT_KILLED);
}

/* R-05: interactive shells keep running after a failed command and must not
 * reserve the error status on their own process; kernel-entry children
 * still do. */
TEST(interactive_shell_does_not_poison_exit_status) {
    setup_shell();
    ss_curr_task = &root_task;
    char kill_bad[] = "kill 999";
    ASSERT_EQ(ss_cli_execute_line(kill_bad), 0);
    char wait_invalid[] = "wait 0";
    ASSERT_EQ(ss_cli_execute_line(wait_invalid), 0);
    char wait_missing[] = "wait 999";
    ASSERT_EQ(ss_cli_execute_line(wait_missing), 0);
    char unknown[] = "frobnicate";
    ASSERT_EQ(ss_cli_execute_line(unknown), 0);
    char unknown_args[] = "help extra";
    ASSERT_EQ(ss_cli_execute_line(unknown_args), 0);
    SSProcessSnapshot snapshot;
    ASSERT_EQ(ss_process_snapshot(1, &snapshot), SS_OK);
    ASSERT_EQ(snapshot.exit_status, 0);

    /* A kernel-entry child still reserves the command's status. */
    char line[] = "kill 999&";
    ASSERT_EQ(ss_cli_execute_line(line), 0);
    execute_child(2);
    uint16_t status;
    ASSERT_EQ(ss_process_wait(1, 2, &status), SS_OK);
    ASSERT_EQ(status, 1);
}

/* U4: the messages printed by ss_cli_execute_line itself, before any
 * command or background child is started. */

TEST(cli_execute_line_rejects_long_line) {
    setup_shell();
    /* SS_CLI_LINE_MAX includes the terminating NUL, so 96 characters are
     * already too long; one byte shorter still runs the trimmed command. */
    char line[SS_CLI_LINE_MAX + 1u];
    memset(line, ' ', sizeof(line));
    line[sizeof(line) - 1u] = '\0';
    memcpy(line, "version", 7u);
    ASSERT_EQ(ss_cli_execute_line(line), 0);
    ASSERT_STR_EQ(output, "Command too long.\r\n");
    SSProcessSnapshot snapshot;
    ASSERT_EQ(ss_process_snapshot(2, &snapshot), SS_ERR_ID);
    output[0] = '\0';
    line[strlen(line) - 1u] = '\0';
    ASSERT_EQ(ss_cli_execute_line(line), 0);
    ASSERT_STR_EQ(output, "SSOS-9 CLI preemptive minimum\r\n");
}

TEST(cli_execute_line_reports_parse_errors) {
    setup_shell();
    uint32_t baseline = ss_memory_free_bytes();
    /* The parser capacity is SS_PROCESS_MAX_ARGS + 1 tokens; the 18th token
     * trips SS_CLI_PARSE_LIMIT before any command name is inspected. */
    char too_many[] = "a b c d e f g h i j k l m n o p q r";
    ASSERT_EQ(ss_cli_execute_line(too_many), 0);
    ASSERT_STR_EQ(output, "Too many arguments.\r\n");
    SSProcessSnapshot snapshot;
    ASSERT_EQ(ss_process_snapshot(2, &snapshot), SS_ERR_ID);
    const char* forms[] = {"&&", "foo & bar", "&"};
    for (unsigned i = 0; i < sizeof(forms) / sizeof(forms[0]); i++) {
        char line[32];
        strcpy(line, forms[i]);
        output[0] = '\0';
        ASSERT_EQ(ss_cli_execute_line(line), 0);
        ASSERT_STR_EQ(output, "Syntax error.\r\n");
        ASSERT_EQ(ss_process_snapshot(2, &snapshot), SS_ERR_ID);
    }
    ASSERT_EQ(ss_memory_free_bytes(), baseline);
}

TEST(cli_execute_line_empty_line_is_silent) {
    setup_shell();
    char empty[] = "";
    ASSERT_EQ(ss_cli_execute_line(empty), 0);
    ASSERT_STR_EQ(output, "");
    char blank[] = "   ";
    ASSERT_EQ(ss_cli_execute_line(blank), 0);
    ASSERT_STR_EQ(output, "");
    SSProcessSnapshot snapshot;
    ASSERT_EQ(ss_process_snapshot(2, &snapshot), SS_ERR_ID);
}

/* Unknown commands reserve their status only in kernel-entry children; the
 * interactive root shell keeps its own exit status clean (R-05, covered by
 * interactive_shell_does_not_poison_exit_status). The lines below contain
 * no trailing '&', so execute_line itself spawns nothing. */
static void* cli_line_entry(void* argument) {
    (void)ss_cli_execute_line(argument);
    return NULL;
}

TEST(cli_execute_line_unknown_status_reserved) {
    setup_shell();
    const char* commands[] = {"frobnicate", "frobnicate now"};
    const uint16_t expected[] = {127, 2};
    const char* messages[] = {"Unknown command. Type 'help'.",
                              "Unknown command or invalid arguments."};
    for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        SSProcessId pid;
        ASSERT_EQ(ss_process_spawn_kernel(1, "cli", cli_line_entry,
                                          commands[i],
                                          (uint16_t)(strlen(commands[i]) + 1u),
                                          &pid), SS_OK);
        output[0] = '\0';
        execute_child(pid);
        ASSERT_NOT_NULL(strstr(output, messages[i]));
        uint16_t status;
        ASSERT_EQ(ss_process_wait(1, pid, &status), SS_OK);
        ASSERT_EQ(status, expected[i]);
    }
}

/* Line editor: cursor keys and DEL arrive as scan<<8 IOCS events; BS and
 * the classic 0x7f spelling delete before the cursor. Exact echo bytes. */
TEST(cli_edit_key_cursor_and_delete) {
    setup_shell();
    char line[SS_CLI_LINE_MAX];
    unsigned used = 0, cursor = 0;
    unsigned cap = (unsigned)sizeof(line);
    for (const char* p = "abc"; *p; p++)
        ASSERT_EQ(ss_cli_edit_key(*p, line, &used, &cursor, cap), 1);
    ASSERT_STR_EQ(line, "abc");
    ASSERT_STR_EQ(output, "abc");
    /* LEFT twice then RIGHT once: the cursor sits on the last character. */
    output[0] = '\0';
    ASSERT_EQ(ss_cli_edit_key(0x3B00, line, &used, &cursor, cap), 1);
    ASSERT_EQ(ss_cli_edit_key(0x3B00, line, &used, &cursor, cap), 1);
    ASSERT_EQ(cursor, 1u);
    ASSERT_EQ(ss_cli_edit_key(0x3D00, line, &used, &cursor, cap), 1);
    ASSERT_EQ(cursor, 2u);
    ASSERT_STR_EQ(output, "\b\bb");
    /* DEL deletes forward at the cursor: "abc" -> "ab", the Human68k
     * prompt behavior. The tail reprint is empty, so the echo just erases
     * the removed character. */
    output[0] = '\0';
    ASSERT_EQ(ss_cli_edit_key(0x3700, line, &used, &cursor, cap), 1);
    ASSERT_STR_EQ(line, "ab");
    ASSERT_STR_EQ(output, " \b");
    /* BS mid-line removes the character before the cursor and redraws the
     * shorter tail, leaving the visible cursor where it was. */
    output[0] = '\0';
    ASSERT_EQ(ss_cli_edit_key(0x3B00, line, &used, &cursor, cap), 1);
    ASSERT_EQ(ss_cli_edit_key(0x08, line, &used, &cursor, cap), 1);
    ASSERT_STR_EQ(line, "b");
    ASSERT_STR_EQ(output, "\b\bb \b\b");
    /* Inserting at the head shifts the tail and steps the cursor back. */
    output[0] = '\0';
    ASSERT_EQ(ss_cli_edit_key('a', line, &used, &cursor, cap), 1);
    ASSERT_STR_EQ(line, "ab");
    ASSERT_STR_EQ(output, "ab\b");
    /* 0x7f deletes before the cursor exactly like BS at the end. */
    ASSERT_EQ(ss_cli_edit_key(0x3D00, line, &used, &cursor, cap), 1);
    output[0] = '\0';
    ASSERT_EQ(ss_cli_edit_key(0x7f, line, &used, &cursor, cap), 1);
    ASSERT_STR_EQ(line, "a");
    ASSERT_STR_EQ(output, "\b \b");
}

TEST(cli_edit_key_bounds_and_unknown) {
    setup_shell();
    char line[8];
    unsigned used = 0, cursor = 0;
    /* Edge no-ops on an empty line are still consumed. */
    ASSERT_EQ(ss_cli_edit_key(0x3B00, line, &used, &cursor, 8u), 1);
    ASSERT_EQ(ss_cli_edit_key(0x3D00, line, &used, &cursor, 8u), 1);
    ASSERT_EQ(ss_cli_edit_key(0x3700, line, &used, &cursor, 8u), 1);
    ASSERT_EQ(used, 0u);
    ASSERT_STR_EQ(output, "");
    /* The capacity counts the terminating NUL: three bytes fill it. */
    for (const char* p = "abc"; *p; p++)
        ASSERT_EQ(ss_cli_edit_key(*p, line, &used, &cursor, 8u), 1);
    ASSERT_EQ(ss_cli_edit_key('d', line, &used, &cursor, 4u), 0);
    ASSERT_STR_EQ(line, "abc");
    ASSERT_EQ(used, 3u);
    /* DEL at the end of the line is a consumed no-op. */
    ASSERT_EQ(ss_cli_edit_key(0x3700, line, &used, &cursor, 8u), 1);
    ASSERT_STR_EQ(line, "abc");
    /* Other special keys (F5 scan 0x67) are not consumed. */
    ASSERT_EQ(ss_cli_edit_key(0x6700, line, &used, &cursor, 8u), 0);
    ASSERT_STR_EQ(line, "abc");
}

void run_cli_background_tests(void) {
    RUN_TEST(process_reaper_claims_before_hook_and_release);
    RUN_TEST(process_syscall_memory_ownership);
    RUN_TEST(process_console_inherited);
    RUN_TEST(cli_run_passes_arguments);
    RUN_TEST(cli_background_builtin_exit_isolated);
    RUN_TEST(cli_background_wait_cannot_reap_parent_child);
    RUN_TEST(cli_background_diagnostics_and_kill);
    RUN_TEST(cli_kernel_child_slot_failure_rolls_back);
    RUN_TEST(cli_invalid_ampersand_has_no_side_effect);
    RUN_TEST(cli_background_builtin_error_status);
    RUN_TEST(orphan_zombie_is_reaped_by_orphan_sweep);
    RUN_TEST(normal_child_wait_result_untouched_by_orphan_sweep);
    RUN_TEST(orphan_cycles_keep_slot_count_stable);
    RUN_TEST(killed_child_wait_reports_kill_status);
    RUN_TEST(interactive_shell_does_not_poison_exit_status);
    RUN_TEST(cli_execute_line_rejects_long_line);
    RUN_TEST(cli_execute_line_reports_parse_errors);
    RUN_TEST(cli_execute_line_empty_line_is_silent);
    RUN_TEST(cli_execute_line_unknown_status_reserved);
    RUN_TEST(cli_edit_key_cursor_and_delete);
    RUN_TEST(cli_edit_key_bounds_and_unknown);
    RUN_TEST(cli_background_cancel_during_output_releases_console);
    RUN_TEST(cli_kernel_child_allocation_and_task_failure_roll_back);
    RUN_TEST(cli_scsi_mount_write_read);
    RUN_TEST(console_mux_routes_output);
    RUN_TEST(console_foreground_input_routing);
    RUN_TEST(console_scroll_matches_virtual_screen);
    RUN_TEST(console_background_scroll_preserves_active_window);
    RUN_TEST(mux_starts_shell_processes);
}
