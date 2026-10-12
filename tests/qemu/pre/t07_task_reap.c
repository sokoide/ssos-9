/* Verify task exit leaves no queued reference and its slot can be reused. */

#include "main_task.h"
#include "tty.h"

static SSTask main_tcb;

static void emulate_timer_tick(void) {
    __asm__ volatile("trap #0" ::: "memory");
}

static void fail(const char* reason) {
    tty_puts("FAIL ");
    tty_puts(reason);
    tty_puts("\n");
    for (;;) emulate_timer_tick();
}

static void* return_immediately(void* arg) {
    (void)arg;
    return 0;
}

int main(void) {
    tty_puts("START task-reap\n");
    ss_sched_init();
    if (ss_main_task_register(&main_tcb, 8) != SS_OK ||
        ss_idle_task_start() != SS_OK)
        fail("bootstrap");

    SSTaskInfo info = {
        .entry = return_immediately,
        .pri = 8,
        .stack_size = SS_TASK_STACK,
    };
    uint16_t first = ss_task_create(&info);
    if (first == 0 || ss_task_start(first) != SS_OK)
        fail("worker start");

    emulate_timer_tick();
    if (tcb_table[first - 1].state != SS_TS_TERMINATED)
        fail("worker did not terminate");
    if (ss_task_release_terminated(first) != SS_OK)
        fail("release terminated task");
    if (tcb_table[first - 1].state != SS_TS_NONE)
        fail("slot not cleared");

    uint16_t second = ss_task_create(&info);
    if (second != first)
        fail("slot not reused");

    tty_puts("OK task slot reused\n");
    for (;;) emulate_timer_tick();
}
