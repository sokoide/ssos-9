/* Verify that an idle task keeps timed waits live when all workers sleep. */

#include "main_task.h"
#include "tty.h"

static SSTask main_tcb;
static volatile unsigned worker_started;

static void emulate_timer_tick(void) {
    __asm__ volatile("trap #0" ::: "memory");
}

static void fail(const char* reason) {
    tty_puts("FAIL ");
    tty_puts(reason);
    tty_puts("\n");
    for (;;) emulate_timer_tick();
}

static void* sleeper(void* arg) {
    (void)arg;
    worker_started = 1;
    for (;;) {
        if (ss_task_sleep(100) != SS_OK)
            fail("worker sleep");
    }
    return 0;
}

int main(void) {
    tty_puts("START idle-sleep\n");
    ss_sched_init();
    if (ss_main_task_register(&main_tcb, 8) != SS_OK)
        fail("main register");
    if (ss_idle_task_start() != SS_OK)
        fail("idle start");

    SSTaskInfo info = {
        .entry = sleeper,
        .pri = 8,
        .stack_size = SS_TASK_STACK,
    };
    uint16_t id = ss_task_create(&info);
    if (id == 0 || ss_task_start(id) != SS_OK)
        fail("worker start");

    if (ss_task_sleep(5) != SS_OK)
        fail("main sleep without ready workers");
    if (!worker_started || ss_tick_counter < 5)
        fail("idle did not advance wait");

    tty_puts("OK idle woke sleepers\n");
    for (;;) emulate_timer_tick();
}
