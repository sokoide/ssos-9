#ifndef SS_PROCESS_H
#define SS_PROCESS_H

#include "../kernel/scheduler.h"
#include "../module/module.h"

#define SS_PROCESS_MAX 8u
#define SS_PROCESS_MAX_ARGS 16u
#define SS_PROCESS_MAX_ARG_BYTES 512u

typedef uint32_t SSProcessId;

typedef enum {
    SS_PROCESS_FREE = 0,
    SS_PROCESS_INITIALIZING,
    SS_PROCESS_READY,
    SS_PROCESS_REAPING,
    SS_PROCESS_ZOMBIE,
} SSProcessState;

typedef struct {
    SSProcessId pid;
    SSProcessId ppid;
    SSModuleId module_id;
    uint16_t task_id;
    uint16_t exit_status;
    uint8_t state;
    char name[SS_MODULE_NAME_CAPACITY];
    void* data_base;
    void* got_base;
    void* stack_low;
    void* stack_high;
} SSProcessSnapshot;

int16_t ss_process_init(SSTask* root_task);
int16_t ss_process_spawn(SSProcessId parent, SSModuleId module_id,
                         uint16_t argc, const char* const argv[],
                         SSProcessId* pid);
/* Trusted kernel-code child, with a copied argument and private stack.
 * Entry returns through the scheduler's normal task-exit/reaper path. */
int16_t ss_process_spawn_kernel(SSProcessId parent, const char* name,
                                void* (*entry)(void*), const void* argument,
                                uint16_t argument_size, SSProcessId* pid);
int16_t ss_process_snapshot(SSProcessId pid, SSProcessSnapshot* snapshot);
int16_t ss_process_snapshot_at(uint16_t index, SSProcessSnapshot* snapshot);
int16_t ss_process_kill(SSProcessId pid);
int16_t ss_process_wait(SSProcessId parent, SSProcessId child,
                        uint16_t* exit_status);
SSProcessId ss_process_current_id(void);
SSProcessId ss_process_current_parent_id(void);
void ss_process_exit_current(uint16_t exit_status);
void ss_process_reap_task(uint16_t task_id);
void ss_process_syscall_enter(void);
int16_t ss_process_syscall_leave(void);
int16_t ss_process_kill_pending(void);

#endif
