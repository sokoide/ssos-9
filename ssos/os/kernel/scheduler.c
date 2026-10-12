#include "scheduler.h"
#include <stdint.h>
#include <string.h>

SSTask tcb_table[SS_MAX_TASKS];
SSReadyQueue ready_queue;
SSTask* ss_curr_task;
SSTask* ss_scheduled_task;
static SSTask* sleeping_tasks;
static void (*reaper_hook)(uint16_t task_id);
static SSTraceEvent trace_events[SS_TRACE_CAPACITY];
static uint8_t trace_next;
static uint8_t trace_count;

static void* idle_task(void* arg) {
    (void)arg;
    for (;;) {
#ifdef SS_QEMU_TEST
        /* QEMU's virt machine has no MFP Timer D; trap #1 drives its test ISR.
         */
        __asm__ volatile("trap #1" ::: "memory");
#elif defined(__m68k__)
        /* Wake on the next enabled interrupt, normally Timer D. */
        __asm__ volatile("stop #0x2000" ::: "memory");
#else
        ss_task_yield();
#endif
    }
    return NULL;
}

static void* reaper_task(void* arg) {
    (void)arg;
    for (;;) {
        for (uint16_t id = 1; id <= SS_MAX_TASKS; id++) {
            if (reaper_hook != NULL)
                reaper_hook(id);
            (void)ss_task_release_terminated(id);
        }
        (void)ss_task_sleep(1);
    }
    return NULL;
}

static uint8_t task_id(const SSTask* task) {
    uintptr_t address = (uintptr_t)task;
    uintptr_t first = (uintptr_t)&tcb_table[0];
    uintptr_t end = (uintptr_t)&tcb_table[SS_MAX_TASKS];
    if (address >= first && address < end &&
        (address - first) % sizeof(tcb_table[0]) == 0) {
        return (uint8_t)((address - first) / sizeof(tcb_table[0]) + 1);
    }
    return task == NULL ? 0xff : 0;
}

static void trace(SSTraceKind kind, const SSTask* task, const SSTask* other,
                  uint32_t value) {
    SSTraceEvent* event = &trace_events[trace_next];
    event->tick = ss_tick_counter;
    event->value = value;
    event->context = task == NULL ? 0 : (uint32_t)(uintptr_t)task->context;
    event->kind = (uint8_t)kind;
    event->task_id = task_id(task);
    event->other_id = task_id(other);
    event->reserved = 0;
    trace_next = (uint8_t)((trace_next + 1) % SS_TRACE_CAPACITY);
    if (trace_count < SS_TRACE_CAPACITY)
        trace_count++;
}

uint16_t ss_trace_read(SSTraceEvent* events, uint16_t capacity) {
    if (events == NULL || capacity == 0)
        return 0;
    uint16_t saved_sr = ss_irq_save();
    uint16_t count = trace_count < capacity ? trace_count : capacity;
    uint8_t start =
        (uint8_t)((trace_next + SS_TRACE_CAPACITY - count) % SS_TRACE_CAPACITY);
    for (uint16_t i = 0; i < count; i++)
        events[i] = trace_events[(start + i) % SS_TRACE_CAPACITY];
    ss_irq_restore(saved_sr);
    return count;
}

/* Stack canary magic number */
#define STACK_CANARY 0xDEADCAFE

/* Write canary at bottom of each task stack, return remaining bytes */
uint32_t ss_stack_check(uint16_t id) {
    if (id == 0 || id > SS_MAX_TASKS)
        return 0;
    SSTask* tcb = &tcb_table[id - 1];
    if (tcb->stack_size == 0)
        return 0;

    /* Bottom of stack = stack_base - stack_size + 4 */
    uint8_t* bottom =
        (uint8_t*)((uintptr_t)tcb->stack_base - tcb->stack_size + 4);

    /* Scan from bottom upward to find first non-canary word */
    uint32_t used = 0;
    for (uint32_t offset = 0; offset < tcb->stack_size; offset += 4) {
        if (*(uint32_t*)(bottom + offset) != STACK_CANARY) {
            used = tcb->stack_size - offset;
            break;
        }
    }
    return tcb->stack_size - used; /* remaining bytes */
}

/* Write canary pattern to entire task stack */
void ss_stack_canary_init(uint16_t id) {
    if (id == 0 || id > SS_MAX_TASKS)
        return;
    SSTask* tcb = &tcb_table[id - 1];
    if (tcb->stack_size == 0)
        return;

    uint8_t* bottom =
        (uint8_t*)((uintptr_t)tcb->stack_base - tcb->stack_size + 4);
    for (uint32_t offset = 0; offset < tcb->stack_size; offset += 4) {
        *(uint32_t*)(bottom + offset) = STACK_CANARY;
    }
}

void ss_sched_init(void) {
    memset(tcb_table, 0, sizeof(tcb_table));
    memset(&ready_queue, 0, sizeof(ready_queue));
    ss_curr_task = NULL;
    ss_scheduled_task = NULL;
    sleeping_tasks = NULL;
    reaper_hook = NULL;
    trace_next = 0;
    trace_count = 0;
}

int16_t ss_idle_task_start(void) {
    SSTaskInfo info = {
        .entry = idle_task,
        .pri = SS_IDLE_PRIORITY,
        .ctx_level = SS_CTX_NORMAL,
        .stack_size = SS_TASK_STACK,
        .stack = NULL,
        .arg = NULL,
    };
    uint16_t id = ss_task_create(&info);
    if (id == 0 || id > SS_MAX_TASKS)
        return (int16_t)id;
    return (int16_t)ss_task_start(id);
}

int16_t ss_reaper_task_start(void) {
    SSTaskInfo info = {
        .entry = reaper_task,
        .pri = 8,
        .ctx_level = SS_CTX_NORMAL,
        .stack_size = SS_TASK_STACK,
        .stack = NULL,
        .arg = NULL,
    };
    uint16_t id = ss_task_create(&info);
    if (id == 0 || id > SS_MAX_TASKS)
        return (int16_t)id;
    return (int16_t)ss_task_start(id);
}

void ss_reaper_set_hook(void (*hook)(uint16_t task_id)) {
    uint16_t saved_sr = ss_irq_save();
    reaper_hook = hook;
    ss_irq_restore(saved_sr);
}

void ss_sched_enqueue(SSTask* tcb) {
    uint8_t pri = tcb->pri;
    if (pri >= SS_MAX_PRI)
        return;

    tcb->next = NULL;
    if (ready_queue.tails[pri] == NULL) {
        ready_queue.heads[pri] = tcb;
        ready_queue.tails[pri] = tcb;
        tcb->prev = NULL;
    } else {
        ready_queue.tails[pri]->next = tcb;
        tcb->prev = ready_queue.tails[pri];
        ready_queue.tails[pri] = tcb;
    }
    ready_queue.pri_bitmap |= (1 << (15 - pri));
}

void ss_sched_dequeue(SSTask* tcb) {
    uint8_t pri = tcb->pri;
    if (pri >= SS_MAX_PRI)
        return;

    if (tcb->prev) {
        tcb->prev->next = tcb->next;
    } else {
        ready_queue.heads[pri] = tcb->next;
    }
    if (tcb->next) {
        tcb->next->prev = tcb->prev;
    } else {
        ready_queue.tails[pri] = tcb->prev;
    }
    tcb->prev = NULL;
    tcb->next = NULL;

    if (ready_queue.heads[pri] == NULL) {
        ready_queue.pri_bitmap &= ~(1 << (15 - pri));
    }
}

SSTask* ss_sched_pick(void) {
    if (ready_queue.pri_bitmap == 0)
        return NULL;

    uint16_t bits = ready_queue.pri_bitmap;
    int pri = 0;
    while (!(bits & 0x8000) && pri < 16) {
        bits <<= 1;
        pri++;
    }
    if (pri >= SS_MAX_PRI)
        return NULL;
    return ready_queue.heads[pri];
}

uint16_t ss_task_create(SSTaskInfo* info) {
    if (info == NULL || info->entry == NULL)
        return SS_ERR_PARAM;
    if (info->pri >= SS_MAX_PRI || info->ctx_level > SS_CTX_FULL)
        return SS_ERR_PARAM;
    if (info->stack != NULL &&
        (((uintptr_t)info->stack & 3u) != 0 ||
         info->stack_size < SS_MIN_TASK_STACK || (info->stack_size & 3u) != 0))
        return SS_ERR_PARAM;

    extern uint8_t* ss_task_stack_base;

    uint16_t saved_sr = ss_irq_save();

    uint16_t i;
    for (i = 0; i < SS_MAX_TASKS; i++) {
        if (tcb_table[i].state == SS_TS_NONE)
            break;
    }
    if (i >= SS_MAX_TASKS) {
        ss_irq_restore(saved_sr);
        return SS_ERR_LIMIT;
    }
    if (info->stack == NULL && ss_task_stack_base == NULL) {
        ss_irq_restore(saved_sr);
        return SS_ERR_STATE;
    }

    SSTask* tcb = &tcb_table[i];
    memset(tcb, 0, sizeof(SSTask));
    tcb->state = SS_TS_INITIALIZING;
    tcb->entry = info->entry;
    tcb->arg = info->arg;
    tcb->pri = info->pri;
    tcb->ctx_level = info->ctx_level;

    if (info->stack != NULL) {
        tcb->stack_base = info->stack;
        tcb->stack_size = info->stack_size;
    } else {
        uintptr_t stack_end =
            (uintptr_t)(ss_task_stack_base + (i + 1) * SS_TASK_STACK);
        tcb->stack_base = (uint8_t*)((stack_end - 4) & ~(uintptr_t)3);
        tcb->stack_size = SS_TASK_STACK;
    }

    tcb->context = tcb->stack_base;

    ss_irq_restore(saved_sr);

    /* Fill the stack canary with interrupts enabled: the slot is
     * reserved as INITIALIZING and rejected by ss_task_start,
     * so keeping this multi-millisecond write inside the masked window would
     * only stretch Timer D / V-DISP latency. */
#ifdef SS_TASK_CREATE_PROBE
    extern void ss_task_create_probe(uint16_t id);
    ss_task_create_probe(i + 1);
#endif
    ss_stack_canary_init(i + 1);
    saved_sr = ss_irq_save();
    tcb->state = SS_TS_DORMANT;
    trace(SS_TRACE_CREATE, tcb, NULL, tcb->stack_size);
    ss_irq_restore(saved_sr);

    return i + 1; /* 1-based ID */
}

uint16_t ss_task_create_interrupted(void* stack, uint16_t stack_size,
                                    const uint32_t regs[SS_FRAME_REG_WORDS],
                                    void* entry, uint16_t sr) {
    if (stack == NULL || entry == NULL || stack_size < SS_MIN_TASK_STACK ||
        (uintptr_t)stack % 4 != 0 || (stack_size & 3u) != 0 || sr != 0x2000u)
        return SS_ERR_PARAM;

    SSTaskInfo info = {
        .entry = (void* (*)(void*))entry,
        .pri = 8,
        .ctx_level = SS_CTX_FULL,
        .stack_size = stack_size,
        .stack = (uint8_t*)stack + stack_size - 4u,
    };
    uint16_t id = ss_task_create(&info);
    if (id == 0 || id > SS_MAX_TASKS)
        return id;

    uint16_t saved_sr = ss_irq_save();
    SSTask* tcb = &tcb_table[id - 1];
    SSInterruptedFrame* frame =
        (SSInterruptedFrame*)((uintptr_t)tcb->stack_base -
                              sizeof(SSInterruptedFrame));
    for (uint32_t i = 0; i < SS_FRAME_REG_WORDS; i++)
        frame->regs[i] = regs == NULL ? 0 : regs[i];
    frame->sr = sr;
    frame->pc = (uint32_t)(uintptr_t)entry;
    *(uint32_t*)tcb->stack_base = (uint32_t)(uintptr_t)ss_task_exit;
    tcb->entry = NULL;
    tcb->arg = NULL;
    tcb->context = frame;
    tcb->resume_type = 0;
    ss_irq_restore(saved_sr);
    return id;
}

uint16_t ss_task_start(uint16_t id) {
    if (id == 0 || id > SS_MAX_TASKS)
        return SS_ERR_ID;

    uint16_t saved_sr = ss_irq_save();
    SSTask* tcb = &tcb_table[id - 1];

    if (tcb->state != SS_TS_DORMANT) {
        ss_irq_restore(saved_sr);
        return SS_ERR_STATE;
    }

    tcb->state = SS_TS_READY;
    ss_sched_enqueue(tcb);
    trace(SS_TRACE_START, tcb, NULL, 0);

    if (ss_curr_task == NULL) {
        ss_curr_task = tcb;
    }

    ss_irq_restore(saved_sr);
    return SS_OK;
}

uint16_t ss_task_cancel(uint16_t id) {
    if (id == 0 || id > SS_MAX_TASKS)
        return SS_ERR_ID;

    uint16_t saved_sr = ss_irq_save();
    SSTask* tcb = &tcb_table[id - 1];
    if (tcb == ss_curr_task) {
        ss_irq_restore(saved_sr);
        return SS_ERR_STATE;
    }

    if (tcb->state == SS_TS_READY) {
        ss_sched_dequeue(tcb);
    } else if (tcb->state == SS_TS_WAIT) {
        SSTask** link = &sleeping_tasks;
        while (*link != NULL && *link != tcb) link = &(*link)->sleep_next;
        if (*link == NULL) {
            ss_irq_restore(saved_sr);
            return SS_ERR_STATE;
        }
        *link = tcb->sleep_next;
        tcb->sleep_next = NULL;
    } else if (tcb->state != SS_TS_DORMANT) {
        ss_irq_restore(saved_sr);
        return SS_ERR_STATE;
    }

    tcb->wait_until = 0;
    tcb->state = SS_TS_TERMINATED;
    trace(SS_TRACE_EXIT, tcb, NULL, 0);
    ss_irq_restore(saved_sr);
    return SS_OK;
}

uint16_t ss_task_release_terminated(uint16_t id) {
    if (id == 0 || id > SS_MAX_TASKS)
        return SS_ERR_ID;

    uint16_t saved_sr = ss_irq_save();
    SSTask* tcb = &tcb_table[id - 1];
    if (tcb == ss_curr_task || tcb->state != SS_TS_TERMINATED) {
        ss_irq_restore(saved_sr);
        return SS_ERR_STATE;
    }

    memset(tcb, 0, sizeof(*tcb));
    ss_irq_restore(saved_sr);
    return SS_OK;
}

void ss_do_context_switch(void) {
    /*
     * Disable interrupts while manipulating the ready queue to prevent
     * the TimerD ISR (which calls ss_do_wakeups → ss_sched_enqueue)
     * from corrupting the queue mid-operation.
     *
     * Interrupts remain disabled on return. Assembly restores the selected
     * task's SR from its yield frame or CPU exception frame.
     */
    (void)ss_irq_save();

    SSTask* curr = ss_curr_task;
    if (curr == NULL) {
        return;
    }

    if (curr->state == SS_TS_READY) {
        ss_sched_dequeue(curr);
        ss_sched_enqueue(curr);
    }

    /* Process due sleepers at the scheduler boundary with interrupts masked. */
    ss_process_wakeups();

    SSTask* next = ss_sched_pick();
    if (next == NULL || next == curr) {
        ss_scheduled_task = curr;
        return;
    }

    ss_scheduled_task = next;
    ss_curr_task = next;
    trace(SS_TRACE_SWITCH, curr, next,
          ((uint32_t)curr->resume_type << 8) | curr->state);
}

uint16_t ss_task_sleep(uint32_t ticks) {
    if (ticks > SS_MAX_SLEEP_TICKS)
        return SS_ERR_PARAM;

    SSTask* curr = ss_curr_task;
    if (curr == NULL)
        return SS_ERR_STATE;

    uint16_t saved_sr = ss_irq_save();
    curr->state = SS_TS_WAIT;
    curr->wait_until = ss_tick_counter + ticks;
    ss_sched_dequeue(curr);
    if (ready_queue.pri_bitmap == 0) {
        curr->state = SS_TS_READY;
        curr->wait_until = 0;
        ss_sched_enqueue(curr);
        ss_irq_restore(saved_sr);
        return SS_ERR_STATE;
    }
    curr->sleep_next = sleeping_tasks;
    sleeping_tasks = curr;
    trace(SS_TRACE_SLEEP, curr, NULL, ticks);
    /* Yield saves the masked SR; resume restores it before returning here. */
    ss_task_yield();
    ss_irq_restore(saved_sr);
    return SS_OK;
}

static int deadline_reached(uint32_t now, uint32_t deadline) {
    return now - deadline <= SS_MAX_SLEEP_TICKS;
}

void ss_do_wakeups(void) {
    uint32_t now = ss_tick_counter;
    SSTask** link = &sleeping_tasks;
    while (*link != NULL) {
        SSTask* tcb = *link;
        if (deadline_reached(now, tcb->wait_until)) {
            *link = tcb->sleep_next;
            tcb->sleep_next = NULL;
            tcb->state = SS_TS_READY;
            tcb->wait_until = 0;
            ss_sched_enqueue(tcb);
            trace(SS_TRACE_WAKE, tcb, NULL, now);
        } else {
            link = &tcb->sleep_next;
        }
    }
}

/* A returned task cannot release or reuse its stack while executing on it. */
void ss_task_exit(void) {
    uint16_t saved_sr = ss_irq_save();
    SSTask* curr = ss_curr_task;
    if (curr != NULL && curr->state == SS_TS_READY) {
        ss_sched_dequeue(curr);
        curr->state = SS_TS_TERMINATED;
        trace(SS_TRACE_EXIT, curr, NULL, 0);
    }
    ss_irq_restore(saved_sr);
    for (;;) ss_task_yield();
}
