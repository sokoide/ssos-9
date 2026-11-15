#include "cli_wait.h"
#include "cli_console.h"

#include "../kernel/scheduler.h"

#define SS_CLI_KEY_QUEUE_CAPACITY 64u
#define SS_CLI_CTRL_C 0x03

static SSProcessId foreground_pid[SS_PROCESS_CONSOLE_COUNT];
static uint16_t pending_keys[SS_PROCESS_CONSOLE_COUNT][SS_CLI_KEY_QUEUE_CAPACITY];
static uint8_t pending_head[SS_PROCESS_CONSOLE_COUNT];
static uint8_t pending_tail[SS_PROCESS_CONSOLE_COUNT];
static uint8_t pending_count[SS_PROCESS_CONSOLE_COUNT];
static uint8_t pending_interrupt[SS_PROCESS_CONSOLE_COUNT];
volatile uint8_t ss_ctrl_c_pending;
static uint8_t input_busy;

/* One reader moves keyboard events into the per-window queue at a time, so
 * the waiting shell cannot reorder keys against its foreground child. The
 * lock may be held across IOCS locking; all owners are cancellation guarded. */
static void input_lock(void) {
    for (;;) {
        uint16_t sr = ss_irq_save();
        if (!input_busy) {
            input_busy = 1;
            ss_irq_restore(sr);
            return;
        }
        ss_irq_restore(sr);
        (void)ss_task_sleep(1);
    }
}

static void input_unlock(void) {
    uint16_t sr = ss_irq_save();
    input_busy = 0;
    ss_irq_restore(sr);
}

static int pop_pending_key(uint8_t id) {
    if (pending_count[id] == 0)
        return -1;
    int key = pending_keys[id][pending_head[id]];
    pending_head[id] = (uint8_t)((pending_head[id] + 1u) % SS_CLI_KEY_QUEUE_CAPACITY);
    pending_count[id]--;
    return key;
}

static void push_pending_key(uint8_t id, uint16_t key) {
    if (pending_count[id] == SS_CLI_KEY_QUEUE_CAPACITY)
        return;
    pending_keys[id][pending_tail[id]] = key;
    pending_tail[id] = (uint8_t)((pending_tail[id] + 1u) % SS_CLI_KEY_QUEUE_CAPACITY);
    pending_count[id]++;
}

void ss_cli_set_foreground_pid(SSProcessId pid) {
    input_lock();
    foreground_pid[ss_process_current_console()] = pid;
    input_unlock();
}

int ss_cli_next_key(void) {
    input_lock();
    int key = pop_pending_key(ss_process_current_console());
    if (key < 0) key = ss_cli_console_read_key();
    input_unlock();
    return key;
}

int ss_cli_foreground_key(void) {
    input_lock();
    uint8_t id = ss_process_current_console();
    int key = -1;
    if (foreground_pid[id] && foreground_pid[id] == ss_process_current_id()) {
        key = pop_pending_key(id);
        if (key < 0) key = ss_cli_console_read_key();
        if (key == SS_CLI_CTRL_C) {
            pending_interrupt[id] = 1;
            key = -1;
        }
    }
    input_unlock();
    return key;
}

int ss_cli_poll_ctrl_c(void) {
    input_lock();
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
        uint16_t key = (uint16_t)next_key;
        if (key == SS_CLI_CTRL_C) {
            interrupted = 1;
            continue;
        }
        push_pending_key(id, key);
    }
    input_unlock();
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
        /* Cheap and idempotent: keep orphan zombies from piling up while a
         * foreground job runs. */
        ss_process_reap_orphans();
        if (ss_process_kill_pending()) return SS_CLI_WAIT_CANCELLED;
        if (ss_cli_poll_ctrl_c()) {
            /* foreground_pid is shared state guarded by input_lock; read it
             * under the lock and decide afterwards (no IOCS under the lock). */
            input_lock();
            SSProcessId foreground = foreground_pid[ss_process_current_console()];
            input_unlock();
            if (foreground != pid)
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
