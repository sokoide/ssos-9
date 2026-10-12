/* Verify cancelling a sleeper unlinks it before its slot is released. */

#include "main_task.h"
#include "tty.h"

static SSTask main_tcb;
static volatile unsigned worker_started;
static volatile unsigned worker_resumed;

static void emulate_timer_tick(void) {
    __asm__ volatile("trap #1" ::: "memory");
}

static void fail(const char* reason) {
    tty_puts("FAIL ");
    tty_puts(reason);
    tty_puts("\n");
    for (;;) emulate_timer_tick();
}

static void* long_sleeper(void* arg) {
    (void)arg;
    worker_started = 1;
    if (ss_task_sleep(100) != SS_OK)
        fail("worker sleep");
    worker_resumed = 1;
    for (;;) emulate_timer_tick();
    return 0;
}

int main(void) {
    tty_puts("START cancel-waiting\n");
    ss_sched_init();
    if (ss_main_task_register(&main_tcb, 8) != SS_OK ||
        ss_idle_task_start() != SS_OK)
        fail("bootstrap");

    SSTaskInfo info = {
        .entry = long_sleeper,
        .pri = 8,
        .stack_size = SS_TASK_STACK,
    };
    uint16_t id = ss_task_create(&info);
    if (id == 0 || ss_task_start(id) != SS_OK)
        fail("worker start");
    if (ss_task_sleep(5) != SS_OK)
        fail("main sleep");

    if (!worker_started || worker_resumed)
        fail("worker wait state");
    if (ss_task_cancel(id) != SS_OK)
        fail("cancel waiting task");
    if (tcb_table[id - 1].state != SS_TS_TERMINATED)
        fail("cancel state");
    if (ss_task_release_terminated(id) != SS_OK)
        fail("release cancelled task");

    tty_puts("OK cancelled sleeper released\n");
    for (;;) emulate_timer_tick();
}
