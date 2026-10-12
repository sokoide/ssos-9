/* Verify the preemptive trap/rte path preserves the live register file. */

#include "scheduler.h"
#include "tty.h"
#include <stdint.h>

#define GOAL 5

static volatile int n1;
static volatile int n2;
volatile uint32_t ss_register_failure;
extern void ss_test_register_roundtrip(void);

static void tick(void) { __asm__ volatile("trap #1" ::: "memory"); }

static void* task1(void* arg) {
    (void)arg;
    for (;;) {
        ss_test_register_roundtrip();
        n1++;
        tick();
    }
}

static void* task2(void* arg) {
    (void)arg;
    for (;;) {
        ss_test_register_roundtrip();
        n2++;
        tick();
    }
}

static SSTask main_tcb;

int main(void) {
    tty_puts("START pre reg-save\n");
    ss_sched_init();
    main_tcb.pri = 8;
    main_tcb.stack_base = (void*)1;
    main_tcb.state = SS_TS_READY;
    ss_curr_task = &main_tcb;
    ss_sched_enqueue(&main_tcb);
    SSTaskInfo t1 = {.entry = task1, .pri = 8, .stack_size = SS_TASK_STACK};
    SSTaskInfo t2 = {.entry = task2, .pri = 8, .stack_size = SS_TASK_STACK};
    uint16_t id1 = ss_task_create(&t1);
    uint16_t id2 = ss_task_create(&t2);
    ss_task_start(id1);
    ss_task_start(id2);
    while ((n1 < GOAL || n2 < GOAL) && !ss_register_failure) tick();
    if (ss_register_failure) {
        tty_puts("FAIL register pattern changed\n");
        for (;;) tick();
    }
    tty_puts("OK pre regs preserved\n");
    for (;;) {
    }
}
