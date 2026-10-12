#include "process.h"

#include "../mem/system_arena.h"

#include <stddef.h>

#define SS_PROCESS_IDLE_PID 0u
#define SS_PROCESS_ROOT_PID 1u

static SSProcessSnapshot process_table[SS_PROCESS_MAX];
static SSTask* process_tasks[SS_PROCESS_MAX];
static void* process_blocks[SS_PROCESS_MAX];
static uint8_t process_in_syscall[SS_PROCESS_MAX];
static uint8_t process_kill_pending[SS_PROCESS_MAX];
static uint32_t next_pid;

static void write_be32(uint8_t* destination, uint32_t value) {
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

static void copy_name(char destination[SS_MODULE_NAME_CAPACITY],
                      const char* source) {
    uint32_t i = 0;
    while (i + 1 < SS_MODULE_NAME_CAPACITY && source[i] != '\0') {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
    for (i++; i < SS_MODULE_NAME_CAPACITY; i++) destination[i] = '\0';
}

static int16_t find_slot_by_pid(SSProcessId pid) {
    for (uint16_t i = 0; i < SS_PROCESS_MAX; i++) {
        if (process_table[i].state != SS_PROCESS_FREE &&
            process_table[i].pid == pid)
            return (int16_t)i;
    }
    return SS_ERR_ID;
}

static int16_t reserve_slot(SSProcessId parent, SSProcessId* pid,
                            uint16_t* slot) {
    uint16_t saved_sr = ss_irq_save();
    int16_t parent_slot = find_slot_by_pid(parent);
    if (parent_slot < 0 ||
        process_table[parent_slot].state != SS_PROCESS_READY) {
        ss_irq_restore(saved_sr);
        return SS_ERR_ID;
    }
    if (next_pid == 0 || next_pid > 0x7fffffffu) {
        ss_irq_restore(saved_sr);
        return SS_ERR_LIMIT;
    }
    for (*slot = 0; *slot < SS_PROCESS_MAX; (*slot)++) {
        if (process_table[*slot].state == SS_PROCESS_FREE)
            break;
    }
    if (*slot == SS_PROCESS_MAX) {
        ss_irq_restore(saved_sr);
        return SS_ERR_LIMIT;
    }
    *pid = next_pid++;
    process_table[*slot].pid = *pid;
    process_table[*slot].ppid = parent;
    process_table[*slot].state = SS_PROCESS_INITIALIZING;
    ss_irq_restore(saved_sr);
    return SS_OK;
}

static void clear_slot(uint16_t slot) {
    uint16_t saved_sr = ss_irq_save();
    volatile uint8_t* bytes = (volatile uint8_t*)&process_table[slot];
    for (uint32_t i = 0; i < sizeof(process_table[slot]); i++) bytes[i] = 0;
    process_tasks[slot] = NULL;
    process_blocks[slot] = NULL;
    process_in_syscall[slot] = 0;
    process_kill_pending[slot] = 0;
    ss_irq_restore(saved_sr);
}

int16_t ss_process_init(SSTask* root_task) {
    if (root_task == NULL)
        return SS_ERR_PARAM;
    uint16_t saved_sr = ss_irq_save();
    for (uint16_t i = 0; i < SS_PROCESS_MAX; i++) {
        volatile uint8_t* bytes = (volatile uint8_t*)&process_table[i];
        for (uint32_t j = 0; j < sizeof(process_table[i]); j++) bytes[j] = 0;
        process_tasks[i] = NULL;
        process_blocks[i] = NULL;
        process_in_syscall[i] = 0;
        process_kill_pending[i] = 0;
    }
    process_table[0].pid = SS_PROCESS_ROOT_PID;
    process_table[0].ppid = SS_PROCESS_IDLE_PID;
    process_table[0].state = SS_PROCESS_READY;
    process_table[0].task_id = 0;
    process_tasks[0] = root_task;
    copy_name(process_table[0].name, "shell");
    process_table[0].stack_low = NULL;
    process_table[0].stack_high = NULL;
    next_pid = SS_PROCESS_ROOT_PID + 1;
    ss_irq_restore(saved_sr);
    ss_reaper_set_hook(ss_process_reap_task);
    return SS_OK;
}

int16_t ss_process_spawn(SSProcessId parent, SSModuleId module_id,
                         uint16_t argc, const char* const argv[],
                         SSProcessId* pid) {
    if (pid == NULL || (argc != 0 && argv == NULL) ||
        argc > SS_PROCESS_MAX_ARGS)
        return SS_ERR_PARAM;

    SSModuleSnapshot module;
    if (ss_module_snapshot(module_id, &module) != SS_OK)
        return SS_ERR_ID;

    uint32_t data_size = module.image.init_size + module.image.bss_size;
    uint32_t argv_size = ((uint32_t)argc + 1u) * 4u;
    uint32_t string_size = 0;
    for (uint16_t i = 0; i < argc; i++) {
        if (argv[i] == NULL)
            return SS_ERR_PARAM;
        uint32_t length = 0;
        while (argv[i][length] != '\0') {
            if (length >= SS_PROCESS_MAX_ARG_BYTES ||
                string_size + length >= SS_PROCESS_MAX_ARG_BYTES)
                return SS_ERR_LIMIT;
            length++;
        }
        string_size += length + 1u;
    }

    uint32_t data_aligned = (data_size + 3u) & ~3u;
    uint32_t argv_offset = data_aligned;
    uint32_t strings_offset = argv_offset + argv_size;
    uint32_t stack_offset = (strings_offset + string_size + 3u) & ~3u;
    if (stack_offset < strings_offset ||
        module.image.stack_size > UINT32_MAX - stack_offset)
        return SS_ERR_LIMIT;
    uint32_t allocation_size = stack_offset + module.image.stack_size;

    uint16_t slot;
    SSProcessId new_pid;
    int16_t status = reserve_slot(parent, &new_pid, &slot);
    if (status != SS_OK)
        return status;

    void* block = ss_memory_alloc(allocation_size);
    if (block == NULL) {
        clear_slot(slot);
        return SS_ERR_LIMIT;
    }
    void* data_base = block;
    void* argv_base = (uint8_t*)block + argv_offset;
    char* strings = (char*)block + strings_offset;
    void* stack_base = (uint8_t*)block + stack_offset;
    const void* code_base = module.image.image + module.image.code_offset;

    status = ss_module_prepare_data(module_id, data_base, data_size, code_base);
    if (status != SS_OK) {
        ss_memory_free(block);
        clear_slot(slot);
        return status;
    }

    uint32_t string_at = 0;
    for (uint16_t i = 0; i < argc; i++) {
        uint32_t length = 0;
        while (argv[i][length] != '\0') length++;
        write_be32((uint8_t*)argv_base + (uint32_t)i * 4u,
                   (uint32_t)(uintptr_t)(strings + string_at));
        for (uint32_t j = 0; j <= length; j++)
            strings[string_at + j] = argv[i][j];
        string_at += length + 1u;
    }
    write_be32((uint8_t*)argv_base + (uint32_t)argc * 4u, 0);

    uint32_t regs[SS_FRAME_REG_WORDS] = {0};
    regs[0] = argc;
    regs[8] = (uint32_t)(uintptr_t)argv_base;
    regs[11] = (uint32_t)(uintptr_t)module.image.image;
    regs[13] = (uint32_t)(uintptr_t)data_base + module.image.got_offset;

    uint16_t task_id = ss_task_create_interrupted(
        stack_base, (uint16_t)module.image.stack_size, regs,
        (void*)(module.image.image + module.image.entry_offset), 0x2000);
    if (task_id == 0 || task_id > SS_MAX_TASKS) {
        ss_module_release(module_id);
        ss_memory_free(block);
        clear_slot(slot);
        return (int16_t)task_id;
    }

    uint16_t saved_sr = ss_irq_save();
    SSProcessSnapshot* process = &process_table[slot];
    process->task_id = task_id;
    process_tasks[slot] = &tcb_table[task_id - 1];
    process->module_id = module_id;
    process->data_base = data_base;
    process->got_base = (uint8_t*)data_base + module.image.got_offset;
    process->stack_low = stack_base;
    process->stack_high = (uint8_t*)stack_base + module.image.stack_size;
    copy_name(process->name, module.name);
    process_blocks[slot] = block;
    process_in_syscall[slot] = 0;
    process_kill_pending[slot] = 0;
    status = ss_task_start(task_id);
    if (status == SS_OK)
        process->state = SS_PROCESS_READY;
    ss_irq_restore(saved_sr);

    if (status != SS_OK) {
        ss_task_cancel(task_id);
        ss_task_release_terminated(task_id);
        ss_module_release(module_id);
        ss_memory_free(block);
        clear_slot(slot);
        return status;
    }
    *pid = new_pid;
    return SS_OK;
}

int16_t ss_process_spawn_kernel(SSProcessId parent, const char* name,
                                void* (*entry)(void*), const void* argument,
                                uint16_t argument_size, SSProcessId* pid) {
    if (name == NULL || entry == NULL || argument == NULL || pid == NULL ||
        argument_size == 0 || argument_size > SS_PROCESS_MAX_ARG_BYTES)
        return SS_ERR_PARAM;
    if (SS_TASK_STACK > UINT16_MAX)
        return SS_ERR_LIMIT;
    uint16_t slot;
    SSProcessId new_pid;
    int16_t status = reserve_slot(parent, &new_pid, &slot);
    if (status != SS_OK)
        return status;
    uint32_t stack_offset = ((uint32_t)argument_size + 3u) & ~3u;
    uint8_t* block = ss_memory_alloc(stack_offset + SS_TASK_STACK);
    if (block == NULL) {
        clear_slot(slot);
        return SS_ERR_LIMIT;
    }
    for (uint16_t i = 0; i < argument_size; i++)
        block[i] = ((const uint8_t*)argument)[i];
    SSTaskInfo info = {entry,
                       8,
                       SS_CTX_NORMAL,
                       SS_TASK_STACK,
                       block + stack_offset + SS_TASK_STACK - 4u,
                       block};
    uint16_t task_id = ss_task_create(&info);
    if (task_id == 0 || task_id > SS_MAX_TASKS) {
        ss_memory_free(block);
        clear_slot(slot);
        return (int16_t)task_id;
    }
    uint16_t saved_sr = ss_irq_save();
    SSProcessSnapshot* process = &process_table[slot];
    process->task_id = task_id;
    process->module_id = 0;
    process->data_base = block;
    process->got_base = NULL;
    process->stack_low = block + stack_offset;
    process->stack_high = block + stack_offset + SS_TASK_STACK;
    copy_name(process->name, name);
    process_tasks[slot] = &tcb_table[task_id - 1];
    process_blocks[slot] = block;
    process_in_syscall[slot] = 0;
    process_kill_pending[slot] = 0;
    status = ss_task_start(task_id);
    if (status == SS_OK)
        process->state = SS_PROCESS_READY;
    ss_irq_restore(saved_sr);
    if (status != SS_OK) {
        ss_task_cancel(task_id);
        ss_task_release_terminated(task_id);
        ss_memory_free(block);
        clear_slot(slot);
        return status;
    }
    *pid = new_pid;
    return SS_OK;
}

int16_t ss_process_snapshot(SSProcessId pid, SSProcessSnapshot* snapshot) {
    if (snapshot == NULL)
        return SS_ERR_PARAM;
    uint16_t saved_sr = ss_irq_save();
    int16_t slot = find_slot_by_pid(pid);
    if (slot < 0) {
        ss_irq_restore(saved_sr);
        return SS_ERR_ID;
    }
    *snapshot = process_table[slot];
    ss_irq_restore(saved_sr);
    return SS_OK;
}

int16_t ss_process_snapshot_at(uint16_t index, SSProcessSnapshot* snapshot) {
    if (snapshot == NULL || index >= SS_PROCESS_MAX)
        return SS_ERR_PARAM;
    uint16_t saved_sr = ss_irq_save();
    *snapshot = process_table[index];
    ss_irq_restore(saved_sr);
    return snapshot->state == SS_PROCESS_FREE ? SS_ERR_ID : SS_OK;
}

int16_t ss_process_kill(SSProcessId pid) {
    uint16_t saved_sr = ss_irq_save();
    int16_t slot = find_slot_by_pid(pid);
    if (slot < 0 || process_table[slot].state != SS_PROCESS_READY ||
        process_table[slot].task_id == 0) {
        ss_irq_restore(saved_sr);
        return SS_ERR_ID;
    }
    uint16_t task_id = process_table[slot].task_id;
    if (process_in_syscall[slot]) {
        process_kill_pending[slot] = 1;
        if (tcb_table[task_id - 1].state == SS_TS_WAIT)
            (void)ss_task_wake(task_id);
        ss_irq_restore(saved_sr);
        return SS_OK;
    }
    int16_t status = (int16_t)ss_task_cancel(task_id);
    ss_irq_restore(saved_sr);
    return status;
}

int16_t ss_process_wait(SSProcessId parent, SSProcessId child,
                        uint16_t* exit_status) {
    if (exit_status == NULL)
        return SS_ERR_PARAM;
    uint16_t saved_sr = ss_irq_save();
    int16_t slot = find_slot_by_pid(child);
    if (slot < 0 || process_table[slot].ppid != parent) {
        ss_irq_restore(saved_sr);
        return SS_ERR_ID;
    }
    if (process_table[slot].state != SS_PROCESS_ZOMBIE) {
        ss_irq_restore(saved_sr);
        return SS_ERR_STATE;
    }
    *exit_status = process_table[slot].exit_status;
    for (uint32_t i = 0; i < sizeof(process_table[slot]); i++)
        ((volatile uint8_t*)&process_table[slot])[i] = 0;
    process_tasks[slot] = NULL;
    process_blocks[slot] = NULL;
    ss_irq_restore(saved_sr);
    return SS_OK;
}

SSProcessId ss_process_current_id(void) {
    uint16_t saved_sr = ss_irq_save();
    SSTask* current = ss_curr_task;
    if (current == process_tasks[0]) {
        ss_irq_restore(saved_sr);
        return SS_PROCESS_ROOT_PID;
    }
    for (uint16_t i = 1; i < SS_PROCESS_MAX; i++) {
        if (process_table[i].state == SS_PROCESS_READY &&
            process_tasks[i] == current) {
            SSProcessId pid = process_table[i].pid;
            ss_irq_restore(saved_sr);
            return pid;
        }
    }
    ss_irq_restore(saved_sr);
    return 0;
}

SSProcessId ss_process_current_parent_id(void) {
    SSProcessId pid = ss_process_current_id();
    uint16_t saved_sr = ss_irq_save();
    int16_t slot = find_slot_by_pid(pid);
    SSProcessId parent = slot < 0 ? 0 : process_table[slot].ppid;
    ss_irq_restore(saved_sr);
    return parent;
}

void ss_process_syscall_enter(void) {
    uint16_t saved_sr = ss_irq_save();
    SSTask* current = ss_curr_task;
    for (uint16_t i = 1; i < SS_PROCESS_MAX; i++) {
        if (process_table[i].state == SS_PROCESS_READY &&
            process_tasks[i] == current) {
            process_in_syscall[i] = 1;
            break;
        }
    }
    ss_irq_restore(saved_sr);
}

int16_t ss_process_syscall_leave(void) {
    uint16_t saved_sr = ss_irq_save();
    SSTask* current = ss_curr_task;
    int16_t pending = 0;
    for (uint16_t i = 1; i < SS_PROCESS_MAX; i++) {
        if (process_table[i].state == SS_PROCESS_READY &&
            process_tasks[i] == current) {
            process_in_syscall[i] = 0;
            pending = process_kill_pending[i];
            break;
        }
    }
    ss_irq_restore(saved_sr);
    return pending;
}

int16_t ss_process_kill_pending(void) {
    uint16_t saved_sr = ss_irq_save();
    SSTask* current = ss_curr_task;
    int16_t pending = 0;
    for (uint16_t i = 1; i < SS_PROCESS_MAX; i++) {
        if (process_table[i].state == SS_PROCESS_READY &&
            process_tasks[i] == current) {
            pending = process_kill_pending[i];
            break;
        }
    }
    ss_irq_restore(saved_sr);
    return pending;
}

void ss_process_exit_current(uint16_t exit_status) {
    SSProcessId pid = ss_process_current_id();
    uint16_t saved_sr = ss_irq_save();
    int16_t slot = find_slot_by_pid(pid);
    if (slot >= 0 && process_table[slot].state == SS_PROCESS_READY)
        process_table[slot].exit_status = exit_status;
    ss_irq_restore(saved_sr);
}

void ss_process_reap_task(uint16_t task_id) {
    if (task_id == 0 || task_id > SS_MAX_TASKS)
        return;

    uint16_t saved_sr = ss_irq_save();
    if (tcb_table[task_id - 1].state != SS_TS_TERMINATED) {
        ss_irq_restore(saved_sr);
        return;
    }
    for (uint16_t i = 0; i < SS_PROCESS_MAX; i++) {
        if (process_table[i].state != SS_PROCESS_READY ||
            process_table[i].task_id != task_id)
            continue;
        void* block = process_blocks[i];
        SSModuleId module_id = process_table[i].module_id;
        SSProcessId exiting_pid = process_table[i].pid;
        process_table[i].state = SS_PROCESS_REAPING;
        ss_irq_restore(saved_sr);
        if (module_id != 0)
            ss_module_release(module_id);
        if (block != NULL)
            ss_memory_free(block);
        saved_sr = ss_irq_save();
        for (uint16_t child = 1; child < SS_PROCESS_MAX; child++) {
            if (process_table[child].state != SS_PROCESS_FREE &&
                process_table[child].ppid == exiting_pid)
                process_table[child].ppid = SS_PROCESS_ROOT_PID;
        }
        process_table[i].state = SS_PROCESS_ZOMBIE;
        process_table[i].data_base = NULL;
        process_table[i].got_base = NULL;
        process_table[i].stack_low = NULL;
        process_table[i].stack_high = NULL;
        process_tasks[i] = NULL;
        process_blocks[i] = NULL;
        ss_irq_restore(saved_sr);
        return;
    }
    ss_irq_restore(saved_sr);
}
