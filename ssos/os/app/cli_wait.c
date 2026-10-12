#include "cli_wait.h"

#include "../kernel/scheduler.h"

#include <x68k/iocs.h>

#define SS_CLI_KEY_QUEUE_CAPACITY 64u
#define SS_CLI_CTRL_C 0x03

static SSProcessId foreground_pid;
static uint8_t pending_keys[SS_CLI_KEY_QUEUE_CAPACITY];
static uint8_t pending_head;
static uint8_t pending_tail;
static uint8_t pending_count;
volatile uint8_t ss_ctrl_c_pending;

static int pop_pending_key(void) {
    if (pending_count == 0)
        return -1;
    int key = pending_keys[pending_head];
    pending_head = (uint8_t)((pending_head + 1u) % SS_CLI_KEY_QUEUE_CAPACITY);
    pending_count--;
    return key;
}

static void push_pending_key(uint8_t key) {
    if (pending_count == SS_CLI_KEY_QUEUE_CAPACITY)
        return;
    pending_keys[pending_tail] = key;
    pending_tail = (uint8_t)((pending_tail + 1u) % SS_CLI_KEY_QUEUE_CAPACITY);
    pending_count++;
}

void ss_cli_set_foreground_pid(SSProcessId pid) { foreground_pid = pid; }

int ss_cli_next_key(void) {
    int key = pop_pending_key();
    if (key >= 0)
        return key;
    if (_iocs_b_keysns() <= 0)
        return -1;
    return _iocs_b_keyinp() & 0xff;
}

int ss_cli_poll_ctrl_c(void) {
    uint16_t saved_sr = ss_irq_save();
    int interrupted = ss_ctrl_c_pending != 0;
    ss_ctrl_c_pending = 0;
    ss_irq_restore(saved_sr);
    while (_iocs_b_keysns() > 0) {
        uint8_t key = (uint8_t)(_iocs_b_keyinp() & 0xff);
        if (key == SS_CLI_CTRL_C) {
            interrupted = 1;
            continue;
        }
        push_pending_key(key);
        if (pending_count == SS_CLI_KEY_QUEUE_CAPACITY)
            break;
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
    while ((status = ss_process_wait(1, pid, exit_status)) == SS_ERR_STATE) {
        if (ss_cli_poll_ctrl_c()) {
            if (foreground_pid != pid)
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
