#include "cli.h"
#include "../kernel/kernel.h"
#include "../kernel/main_task.h"
#include "../kernel/scheduler.h"
#include "../mem/system_arena.h"
#include "../module/builtin.h"
#include "../module/module.h"
#include "../process/process.h"
#include "cli_console.h"
#include "cli_parse.h"
#include "cli_wait.h"

#include <stdint.h>
#include <string.h>

#ifndef SS_HOST_TEST
uint8_t* ss_task_stack_base;
#endif
static SSTask main_tcb;
static volatile uint32_t heartbeat;
#ifdef LOCAL_MODE
static uint8_t local_task_stacks[SS_MAX_TASKS * SS_TASK_STACK]
    __attribute__((aligned(4)));
#endif

static void print(const char* text) { ss_cli_console_print(text); }

static void clear_screen(void) { ss_cli_console_clear(); }

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

static const char* process_state_name(uint8_t state) {
    switch (state) {
    case SS_PROCESS_INITIALIZING: return "init";
    case SS_PROCESS_READY: return "ready";
    case SS_PROCESS_REAPING: return "reaping";
    case SS_PROCESS_ZOMBIE: return "zombie";
    default: return "?";
    }
}

static void show_processes(void) {
    print("PID PPID STATE NAME MODULE DATA GOT STACK\r\n");
    for (uint16_t i = 0; i < SS_PROCESS_MAX; i++) {
        SSProcessSnapshot process;
        if (ss_process_snapshot_at(i, &process) != SS_OK)
            continue;
        print_u32(process.pid);
        print(" ");
        print_u32(process.ppid);
        print(" ");
        print(process_state_name(process.state));
        print(" ");
        print(process.name);
        print(" ");
        print_hex32(process.module_id);
        print(" ");
        print_hex32((uint32_t)(uintptr_t)process.data_base);
        print(" ");
        print_hex32((uint32_t)(uintptr_t)process.got_base);
        print(" ");
        print_hex32((uint32_t)(uintptr_t)process.stack_low);
        print("\r\n");
    }
}

static void show_modules(void) {
    print("ID NAME STORAGE IMAGE CODE ENTRY BYTES REFS\r\n");
    for (uint16_t i = 0; i < SS_MODULE_MAX_MODULES; i++) {
        SSModuleSnapshot module;
        if (ss_module_snapshot_at(i, &module) != SS_OK)
            continue;
        print_hex32(module.id);
        print(" ");
        print(module.name);
        print(module.storage == SS_MODULE_STORAGE_BUILTIN ? " builtin "
                                                          : " dynamic ");
        uint32_t base = (uint32_t)(uintptr_t)module.image.image;
        print_hex32(base);
        print(" ");
        print_hex32(base + module.image.code_offset);
        print(" ");
        print_hex32(base + module.image.entry_offset);
        print(" ");
        print_u32(module.image.image_size);
        print(" ");
        print_u32(module.ref_count);
        print("\r\n");
    }
}

static void show_memory(void) {
    /* Capture both values before printing; reaping can change the free list. */
    uint16_t saved_sr = ss_irq_save();
    uint32_t free_bytes = ss_memory_free_bytes();
    uint32_t largest_free = ss_memory_largest_free();
    ss_irq_restore(saved_sr);
    print("arena free_bytes=");
    print_u32(free_bytes);
    print(" largest_free=");
    print_u32(largest_free);
    print("\r\n");
}

static int parse_pid(const char* text, SSProcessId* pid) {
    if (text == NULL || *text == '\0')
        return 0;
    uint32_t value = 0;
    while (*text != '\0') {
        if (*text < '0' || *text > '9')
            return 0;
        uint32_t digit = (uint32_t)(*text++ - '0');
        if (value > (0x7fffffffu - digit) / 10u)
            return 0;
        value = value * 10u + digit;
    }
    if (value == 0)
        return 0;
    *pid = value;
    return 1;
}

static int run_process_command(char* tokens[], uint16_t count, int background) {
    if (strcmp(tokens[0], "ps") == 0 && count == 1) {
        show_processes();
    } else if (strcmp(tokens[0], "modules") == 0 && count == 1) {
        show_modules();
    } else if (strcmp(tokens[0], "mem") == 0 && count == 1) {
        show_memory();
    } else if (strcmp(tokens[0], "run") == 0 && count == 2 &&
               strcmp(tokens[1], "counter") == 0) {
        SSModuleId module_id;
        SSProcessId pid;
        const char* argv[] = {"counter"};
        if (ss_module_find("counter", &module_id) != SS_OK ||
            ss_process_spawn(ss_process_current_id(), module_id, 1, argv,
                             &pid) != SS_OK) {
            print("Unable to start counter.\r\n");
        } else {
            if (background) {
                print("Started counter as PID ");
                print_u32(pid);
                print("\r\n");
            } else {
                uint16_t exit_status;
                int interrupted = 0;
                ss_cli_set_foreground_pid(pid);
                int16_t status =
                    ss_cli_wait_pid(pid, &exit_status, &interrupted);
                ss_cli_set_foreground_pid(0);
                if (status == SS_OK) {
                    if (interrupted)
                        print("^C\r\n");
                    print("PID ");
                    print_u32(pid);
                    print(interrupted ? " interrupted\r\n"
                                      : " exited with status ");
                    if (!interrupted) {
                        print_u32(exit_status);
                        print("\r\n");
                    }
                } else {
                    print("Unable to wait for PID.\r\n");
                }
            }
        }
    } else if (strcmp(tokens[0], "kill") == 0 && count == 2) {
        SSProcessId pid;
        if (!parse_pid(tokens[1], &pid) || ss_process_kill(pid) != SS_OK) {
            print("Cannot kill that PID.\r\n");
            ss_process_exit_current(1);
        } else
            print("Kill requested. Use wait PID to collect it.\r\n");
    } else if (strcmp(tokens[0], "wait") == 0 && count == 2) {
        SSProcessId pid;
        uint16_t exit_status;
        if (!parse_pid(tokens[1], &pid)) {
            print("Invalid PID.\r\n");
            ss_process_exit_current(2);
            return 0;
        }
        int16_t status;
        if (ss_process_current_id() == 1) {
            status = ss_cli_wait_pid(pid, &exit_status, NULL);
        } else {
            /* A subshell cannot wait for the parent shell's children, and
             * must never poll or consume the parent's keyboard input. */
            while ((status = ss_process_wait(ss_process_current_id(), pid,
                                             &exit_status)) == SS_ERR_STATE &&
                   !ss_process_kill_pending())
                (void)ss_task_sleep(1);
        }
        if (status == SS_CLI_WAIT_CANCELLED) {
            print("^C\r\n");
            return 0;
        }
        if (status == SS_OK) {
            print("PID ");
            print_u32(pid);
            print(" exited with status ");
            print_u32(exit_status);
            print("\r\n");
        } else {
            print("No child with that PID.\r\n");
            ss_process_exit_current(127);
        }
    } else {
        return -1;
    }
    return 0;
}

#ifdef LOCAL_MODE
static void stop_children(void) {
    for (uint16_t i = 0; i < SS_PROCESS_MAX; i++) {
        SSProcessSnapshot process;
        if (ss_process_snapshot_at(i, &process) != SS_OK || process.pid == 1)
            continue;
        if (process.state == SS_PROCESS_READY)
            (void)ss_process_kill(process.pid);
        uint16_t exit_status;
        int16_t status;
        while ((status = ss_process_wait(1, process.pid, &exit_status)) ==
               SS_ERR_STATE)
            (void)ss_task_sleep(1);
    }
}
#endif

int16_t ss_init(void) {
#ifdef LOCAL_MODE
    ss_task_stack_base = local_task_stacks;
#else
    ss_task_stack_base = &__ssosstack_start;
#endif
    if (ss_memory_init() != SS_OK)
        return SS_ERR_STATE;
    ss_module_registry_init();
    if (ss_builtin_modules_init() != SS_OK)
        return SS_ERR_STATE;
    ss_sched_init();
    int16_t status = ss_main_task_register(&main_tcb, 8);
    if (status != SS_OK)
        return status;
    status = ss_process_init(&main_tcb);
    if (status != SS_OK)
        return status;
    status = ss_idle_task_start();
    if (status != SS_OK)
        return status;
    return ss_reaper_task_start();
}

static void show_help(void) {
    print("Commands: help, uptime, ps, modules, mem, run counter [&], kill "
          "PID, wait PID, ");
    print("debug, trace, clear, version, exit\r\n");
}

static void* background_builtin(void* argument) {
    ss_process_syscall_enter();
    (void)ss_cli_execute_line(argument);
    if (ss_process_syscall_leave())
        ss_process_exit_current(130);
    return NULL;
}

int ss_cli_execute_line(char* line) {
    char command_copy[64];
    size_t length = strlen(line);
    if (length >= sizeof(command_copy)) {
        print("Command too long.\r\n");
        return 0;
    }
    memcpy(command_copy, line, length + 1u);
    char* tokens[4];
    int background;
    int16_t count = ss_cli_parse_line(line, tokens, 4, &background);
    if (count < 0) {
        print(count == SS_CLI_PARSE_LIMIT ? "Too many arguments.\r\n"
                                          : "Syntax error.\r\n");
        return 0;
    }
    if (count == 0)
        return 0;
    if (background && strcmp(tokens[0], "run") != 0) {
        /* The parser already verified that this is the sole trailing '&'. */
        while (length != 0 && command_copy[length - 1] != '&') length--;
        command_copy[length - 1] = '\0';
        SSProcessId pid;
        if (ss_process_spawn_kernel(ss_process_current_id(), tokens[0],
                                    background_builtin, command_copy,
                                    (uint16_t)length, &pid) != SS_OK) {
            print("Unable to start background command.\r\n");
        } else {
            print("Started command as PID ");
            print_u32(pid);
            print("\r\n");
        }
        return 0;
    }
    int process_result =
        run_process_command(tokens, (uint16_t)count, background);
    if (process_result >= 0)
        return 0;
    if (count != 1) {
        print("Unknown command or invalid arguments.\r\n");
        ss_process_exit_current(2);
        return 0;
    }
    const char* command = tokens[0];
    if (strcmp(command, "help") == 0) {
        show_help();
    } else if (strcmp(command, "uptime") == 0) {
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
    } else if (strcmp(command, "debug") == 0) {
        show_debug();
    } else if (strcmp(command, "trace") == 0) {
        show_trace();
    } else if (strcmp(command, "clear") == 0) {
        clear_screen();
    } else if (strcmp(command, "version") == 0) {
        print("SSOS-9 CLI preemptive minimum\r\n");
    } else if (strcmp(command, "exit") == 0) {
        if (ss_process_current_id() != 1)
            return 1;
#ifdef LOCAL_MODE
        stop_children();
        return 1;
#else
        print("Cannot exit the initial SSOS-9 process (PID 1).\r\n");
#endif
    } else {
        print("Unknown command. Type 'help'.\r\n");
        ss_process_exit_current(127);
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
        if (ss_cli_poll_ctrl_c()) {
            used = 0;
            print("^C\r\n> ");
            continue;
        }
        int key = ss_cli_next_key();
        if (key < 0)
            continue;
        if (key == '\r' || key == '\n') {
            line[used] = '\0';
            print("\r\n");
            if (ss_cli_execute_line(line)) {
                print("SSOS-9 terminated.\r\n");
                return 0;
            }
            used = 0;
            print("> ");
        } else if (key == 0x03) {
            used = 0;
            print("^C\r\n> ");
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
