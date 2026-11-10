#include "cli_wait.h"
#include "cli_console.h"

#include "../kernel/scheduler.h"

#define SS_CLI_KEY_QUEUE_CAPACITY 64u
#define SS_CLI_CTRL_C 0x03

static SSProcessId foreground_pid[SS_PROCESS_CONSOLE_COUNT];
static uint8_t pending_keys[SS_PROCESS_CONSOLE_COUNT][SS_CLI_KEY_QUEUE_CAPACITY];
static uint8_t pending_head[SS_PROCESS_CONSOLE_COUNT];
static uint8_t pending_tail[SS_PROCESS_CONSOLE_COUNT];
static uint8_t pending_count[SS_PROCESS_CONSOLE_COUNT];
static uint8_t pending_interrupt[SS_PROCESS_CONSOLE_COUNT];
volatile uint8_t ss_ctrl_c_pending;

static int pop_pending_key(uint8_t id) {
    if (pending_count[id] == 0)
        return -1;
    int key = pending_keys[id][pending_head[id]];
    pending_head[id] = (uint8_t)((pending_head[id] + 1u) % SS_CLI_KEY_QUEUE_CAPACITY);
    pending_count[id]--;
    return key;
}

static void push_pending_key(uint8_t id, uint8_t key) {
    if (pending_count[id] == SS_CLI_KEY_QUEUE_CAPACITY)
        return;
    pending_keys[id][pending_tail[id]] = key;
    pending_tail[id] = (uint8_t)((pending_tail[id] + 1u) % SS_CLI_KEY_QUEUE_CAPACITY);
    pending_count[id]++;
}

void ss_cli_set_foreground_pid(SSProcessId pid) {
    foreground_pid[ss_process_current_console()] = pid;
}

int ss_cli_next_key(void) {
    int key = pop_pending_key(ss_process_current_console());
    if (key >= 0)
        return key;
    return ss_cli_console_read_key();
}

int ss_cli_poll_ctrl_c(void) {
    uint8_t id = ss_process_current_console();
    uint16_t saved_sr = ss_irq_save();
    if (ss_ctrl_c_pending)
        pending_interrupt[ss_cli_console_mux_active()] = 1;
    ss_ctrl_c_pending = 0;
    int interrupted = pending_interrupt[id];
    pending_interrupt[id] = 0;
    ss_irq_restore(saved_sr);
    int next_key;
    while (pending_count[id] < SS_CLI_KEY_QUEUE_CAPACITY &&
           (next_key = ss_cli_console_read_key()) >= 0) {
        uint8_t key = (uint8_t)next_key;
        if (key == SS_CLI_CTRL_C) {
            interrupted = 1;
            continue;
        }
        push_pending_key(id, key);
    }
    return interrupted;
}

int16_t ss_cli_wait_pid(SSProcessId pid, uint16_t* exit_status,
                        int* interrupted) {
    if (exit_status == NULL)
        return SS_ERR_PARAM;
    if (interrupted != NULL)
        *interrupted = 0;

    int kill_requested = 0;
    int16_t status;
    while ((status = ss_process_wait(ss_process_current_id(), pid, exit_status)) == SS_ERR_STATE) {
        if (ss_process_kill_pending()) return SS_CLI_WAIT_CANCELLED;
        if (ss_cli_poll_ctrl_c()) {
            if (foreground_pid[ss_process_current_console()] != pid)
                return SS_CLI_WAIT_CANCELLED;
            if (!kill_requested) {
                status = ss_process_kill(pid);
                if (status != SS_OK && status != SS_ERR_ID &&
                    status != SS_ERR_STATE)
                    return status;
                kill_requested = 1;
                if (status == SS_OK && interrupted != NULL)
                    *interrupted = 1;
            }
        }
        (void)ss_task_sleep(1);
    }
    return status;
}
