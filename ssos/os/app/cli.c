#include "../kernel/kernel.h"
#include "../kernel/main_task.h"
#include "../kernel/scheduler.h"

#include <stdint.h>
#include <string.h>
#include <x68k/iocs.h>

uint8_t* ss_task_stack_base;
static SSTask main_tcb;
static volatile uint32_t heartbeat;
#ifdef LOCAL_MODE
static uint8_t local_task_stacks[SS_MAX_TASKS * SS_TASK_STACK]
    __attribute__((aligned(4)));
#endif

static void print(const char* text) { _iocs_b_print(text); }

static void clear_screen(void) {
    _iocs_b_clr_st();
    _iocs_b_locate(0, 0);
}

static void print_hex32(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char text[9];
    for (int i = 7; i >= 0; i--) {
        text[i] = digits[value & 0xf];
        value >>= 4;
    }
    text[8] = '\0';
    print(text);
}

static void print_u32(uint32_t value) {
    char digits[10];
    char text[11];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < sizeof(digits));
    unsigned i = 0;
    while (count != 0) text[i++] = digits[--count];
    text[i] = '\0';
    print(text);
}

static const char* trace_name(uint8_t kind) {
    switch (kind) {
    case SS_TRACE_CREATE: return "create";
    case SS_TRACE_START: return "start";
    case SS_TRACE_SLEEP: return "sleep";
    case SS_TRACE_WAKE: return "wake";
    case SS_TRACE_SWITCH: return "switch";
    case SS_TRACE_EXIT: return "exit";
    default: return "?";
    }
}

static void show_debug(void) {
    uint16_t saved_sr = ss_irq_save();
    uint32_t ticks = ss_tick_counter;
    uint32_t beat = heartbeat;
    SSTask* current = ss_curr_task;
    SSTask* scheduled = ss_scheduled_task;
    ss_irq_restore(saved_sr);

    print("ticks=");
    print_hex32(ticks);
    print(" heartbeat=");
    print_hex32(beat);
    print(" current=");
    print_hex32((uint32_t)(uintptr_t)current);
    print(" scheduled=");
    print_hex32((uint32_t)(uintptr_t)scheduled);
    print("\r\nMFP IERB/IMRB/TCDCR/TDDR=");
    print_hex32(((uint32_t)*(volatile uint8_t*)0xe88009 << 24) |
                ((uint32_t)*(volatile uint8_t*)0xe88015 << 16) |
                ((uint32_t)*(volatile uint8_t*)0xe8801d << 8) |
                *(volatile uint8_t*)0xe88025);
    print("\r\n");
    print("MFP IPRA/IPRB/ISRA/ISRB=");
    print_hex32(((uint32_t)*(volatile uint8_t*)0xe8800b << 24) |
                ((uint32_t)*(volatile uint8_t*)0xe8800d << 16) |
                ((uint32_t)*(volatile uint8_t*)0xe8800f << 8) |
                *(volatile uint8_t*)0xe88011);
    print("\r\n");

    for (uint16_t id = 1; id <= SS_MAX_TASKS; id++) {
        SSTask* task = &tcb_table[id - 1];
        if (task->state == SS_TS_NONE)
            continue;
        print("task ");
        print_u32(id);
        print(" state=");
        print_u32(task->state);
        print(" pri=");
        print_u32(task->pri);
        print(" sp=");
        print_hex32((uint32_t)(uintptr_t)task->context);
        uint16_t irq_state = ss_irq_save();
        uint32_t stack_free = ss_stack_check(id);
        ss_irq_restore(irq_state);
        print(" stack_free=");
        print_u32(stack_free);
        print("/");
        print_u32(task->stack_size);
        print("\r\n");
    }
}

static void show_trace(void) {
    SSTraceEvent events[SS_TRACE_CAPACITY];
    uint16_t count = ss_trace_read(events, SS_TRACE_CAPACITY);
    print("scheduler events: ");
    print_u32(count);
    print(" (oldest first)\r\n");
    for (uint16_t i = 0; i < count; i++) {
        SSTraceEvent* event = &events[i];
        print("t=");
        print_hex32(event->tick);
        print(" ");
        print(trace_name(event->kind));
        print(" task=");
        print_u32(event->task_id);
        print(" other=");
        print_u32(event->other_id);
        print(" value=");
        print_hex32(event->value);
        print(" ctx=");
        print_hex32(event->context);
        print("\r\n");
    }
}

static void* heartbeat_task(void* arg) {
    (void)arg;
    for (;;) {
        heartbeat++;
        ss_task_sleep(10);
    }
    return NULL;
}

int16_t ss_init(void) {
#ifdef LOCAL_MODE
    ss_task_stack_base = local_task_stacks;
#else
    ss_task_stack_base = &__ssosstack_start;
#endif
    ss_sched_init();
    return ss_main_task_register(&main_tcb, 8);
}

static void show_help(void) {
    print("Commands: help, uptime, debug, trace, clear, version, exit\r\n");
}

static int run_command(char* line) {
    if (strcmp(line, "help") == 0) {
        show_help();
    } else if (strcmp(line, "uptime") == 0) {
        char out[48] = "worker wakes: ";
        uint32_t value = heartbeat;
        char digits[11];
        unsigned count = 0;
        do {
            digits[count++] = (char)('0' + value % 10);
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count != 0) {
            size_t len = strlen(out);
            out[len] = digits[--count];
            out[len + 1] = '\0';
        }
        strcat(out, "\r\n");
        print(out);
    } else if (strcmp(line, "debug") == 0) {
        show_debug();
    } else if (strcmp(line, "trace") == 0) {
        show_trace();
    } else if (strcmp(line, "clear") == 0) {
        clear_screen();
    } else if (strcmp(line, "version") == 0) {
        print("SSOS-9 CLI preemptive minimum\r\n");
    } else if (strcmp(line, "exit") == 0) {
#ifdef LOCAL_MODE
        return 1;
#else
        print("Cannot exit the initial SSOS-9 process (PID 1).\r\n");
#endif
    } else if (line[0] != '\0') {
        print("Unknown command. Type 'help'.\r\n");
    }
    return 0;
}

int ss_run(void) {
    SSTaskInfo worker = {heartbeat_task, 8,    SS_CTX_NORMAL,
                         SS_TASK_STACK,  NULL, NULL};
    uint16_t worker_id = ss_task_create(&worker);
    if (worker_id == 0 || worker_id > SS_MAX_TASKS ||
        ss_task_start(worker_id) != SS_OK) {
        (void)ss_irq_save();
        for (;;) {
        }
    }

    clear_screen();
    print("SSOS-9: minimal X68000 CLI\r\n");
    print("Type 'help' for commands.\r\n\r\n> ");

    char line[64];
    unsigned used = 0;
    for (;;) {
        if (_iocs_b_keysns() <= 0)
            continue;
        int key = _iocs_b_keyinp() & 0xff;
        if (key == '\r' || key == '\n') {
            line[used] = '\0';
            print("\r\n");
            if (run_command(line)) {
                print("SSOS-9 terminated.\r\n");
                return 0;
            }
            used = 0;
            print("> ");
        } else if (key == 0x08 || key == 0x7f) {
            if (used != 0) {
                used--;
                print("\b \b");
            }
        } else if (key >= 0x20 && key < 0x7f && used + 1 < sizeof(line)) {
            line[used++] = (char)key;
            char echo[2] = {(char)key, '\0'};
            print(echo);
        }
    }
}
