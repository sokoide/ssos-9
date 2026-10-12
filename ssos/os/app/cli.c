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
    print("Commands: help, uptime, clear, version, exit\r\n");
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
