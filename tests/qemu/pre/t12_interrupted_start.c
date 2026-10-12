/* Start a task from a synthetic SR/PC frame and verify its initial registers.
 */

#include "main_task.h"
#include "tty.h"

static SSTask main_tcb;
static uint8_t task_stack[SS_TASK_STACK] __attribute__((aligned(4)));
volatile uint32_t seen_d0;
volatile uint32_t seen_a0;
volatile uint32_t seen_a3;
volatile uint32_t seen_a5;
extern void ss_process_frame_entry(void);

static void emulate_timer_tick(void) {
    __asm__ volatile("trap #0" ::: "memory");
}

static void fail(const char* reason) {
    tty_puts("FAIL ");
    tty_puts(reason);
    tty_puts("\n");
    for (;;) emulate_timer_tick();
}

int main(void) {
    uint32_t regs[SS_FRAME_REG_WORDS] = {0};
    tty_puts("START interrupted-start\n");
    ss_sched_init();
    if (ss_main_task_register(&main_tcb, 8) != SS_OK ||
        ss_idle_task_start() != SS_OK)
        fail("scheduler setup");
    regs[0] = 0x12345678;
    regs[8] = 0x23456789;
    regs[11] = 0x3456789a;
    regs[13] = 0x456789ab;
    uint16_t invalid = ss_task_create_interrupted(
        task_stack, sizeof(task_stack), regs, ss_process_frame_entry, 0x2700);
    if (invalid <= SS_MAX_TASKS)
        fail("invalid initial SR accepted");
    uint16_t id = ss_task_create_interrupted(
        task_stack, sizeof(task_stack), regs, ss_process_frame_entry, 0x2000);
    if (id == 0 || id > SS_MAX_TASKS || ss_task_start(id) != SS_OK)
        fail("frame task create/start");
    for (unsigned i = 0; i < 8 && seen_d0 == 0; i++) emulate_timer_tick();
    if (seen_d0 != regs[0] || seen_a0 != regs[8] || seen_a3 != regs[11] ||
        seen_a5 != regs[13])
        fail("initial registers not restored");
    if (ss_task_release_terminated(id) != SS_OK)
        fail("frame task not terminated");
    tty_puts("OK interrupted frame registers\n");
    for (;;) emulate_timer_tick();
}
