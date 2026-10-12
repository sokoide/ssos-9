/* Host substitutes for the scheduler's CPU-specific operations. */
#include "kernel.h"
#include "scheduler.h"

#include <stdint.h>

uint16_t ss_irq_save(void) { return 0x2000; }
void ss_irq_restore(uint16_t saved_sr) { (void)saved_sr; }

static uint16_t initializing_probe_result;

void ss_task_create_probe(uint16_t id) {
    initializing_probe_result = ss_task_start(id);
}

uint16_t ss_test_create_probe_result(void) { return initializing_probe_result; }

volatile uint32_t ss_tick_counter;

void ss_task_yield(void) { ss_do_context_switch(); }

static uint8_t test_stack_mem[SS_MAX_TASKS * SS_TASK_STACK]
    __attribute__((aligned(4)));
uint8_t* ss_task_stack_base = test_stack_mem;
