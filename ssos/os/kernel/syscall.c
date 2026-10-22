#include "syscall.h"

#include "../../include/ssos9/syscall.h"
#include "../module/module.h"
#include "../process/process.h"
#include "scheduler.h"

static int valid_buffer(uint32_t address, uint32_t size, int write) {
    return (address & 3u) == 0 &&
           ss_process_memory_access(address, size, write);
}

static int copy_string(uint32_t address, char* out, uint32_t capacity) {
    for (uint32_t i = 0; i < capacity; i++) {
        if (address > UINT32_MAX - i ||
            !ss_process_memory_access(address + i, 1, 0))
            return 0;
        out[i] = *(const char*)(uintptr_t)(address + i);
        if (out[i] == '\0')
            return 1;
    }
    return 0;
}

static int32_t process_info(uint32_t pid, uint32_t address) {
    if (!valid_buffer(address, sizeof(SS9ProcessInfo), 1))
        return SS9_ERR_PARAM;
    SSProcessSnapshot snapshot;
    if (ss_process_snapshot(pid, &snapshot) != SS_OK)
        return SS9_ERR;
    SS9ProcessInfo* out = (SS9ProcessInfo*)(uintptr_t)address;
    out->pid = snapshot.pid;
    out->ppid = snapshot.ppid;
    out->exit_status = snapshot.exit_status;
    out->state = snapshot.state;
    out->reserved = 0;
    for (uint16_t i = 0; i < sizeof(out->name); i++)
        out->name[i] = snapshot.name[i];
    return SS_OK;
}

int32_t ss_syscall_dispatch(uint32_t number, uint32_t request) {
    switch (number) {
    case SS9_SYS_VERSION: return SS9_ABI_VERSION;
    case SS9_SYS_GETPID: return (int32_t)ss_process_current_id();
    case SS9_SYS_GETPPID: return (int32_t)ss_process_current_parent_id();
    case SS9_SYS_SPAWN: {
        if (!valid_buffer(request, sizeof(SS9SpawnRequest), 0))
            return SS9_ERR_PARAM;
        SS9SpawnRequest req = *(const SS9SpawnRequest*)(uintptr_t)request;
        if (req.reserved != 0 || req.argc > SS_PROCESS_MAX_ARGS)
            return SS9_ERR_PARAM;
        char name[SS_MODULE_NAME_CAPACITY];
        char arguments[SS_PROCESS_MAX_ARG_BYTES];
        const char* argv[SS_PROCESS_MAX_ARGS];
        if (!copy_string(req.name, name, sizeof(name)) ||
            (req.argc != 0 &&
             !valid_buffer(req.argv, (uint32_t)req.argc * 4u, 0)))
            return SS9_ERR_PARAM;
        uint32_t used = 0;
        for (uint16_t i = 0; i < req.argc; i++) {
            uint32_t source = ((const uint32_t*)(uintptr_t)req.argv)[i];
            if (!copy_string(source, arguments + used,
                             sizeof(arguments) - used))
                return SS9_ERR_PARAM;
            argv[i] = arguments + used;
            while (arguments[used++] != '\0') {
            }
        }
        SSModuleId module_id;
        SSProcessId pid;
        int16_t status = ss_module_find(name, &module_id);
        if (status != SS_OK)
            return SS9_ERR;
        status = ss_process_spawn(ss_process_current_id(), module_id, req.argc,
                                  argv, &pid);
        return status == SS_OK ? (int32_t)pid : status;
    }
    case SS9_SYS_EXIT:
        if (ss_process_current_id() == 1)
            return SS9_ERR_PARAM;
        ss_process_exit_current((uint16_t)request);
        ss_task_exit();
    case SS9_SYS_WAIT: {
        if (!valid_buffer(request, sizeof(SS9WaitRequest), 1))
            return SS9_ERR_PARAM;
        SS9WaitRequest* req = (SS9WaitRequest*)(uintptr_t)request;
        if (req->reserved != 0)
            return SS9_ERR_PARAM;
        uint32_t child = req->pid;
        uint16_t result;
        int16_t status;
        while ((status = ss_process_wait(ss_process_current_id(), child,
                                         &result)) == SS_ERR_STATE)
            if (!ss_process_kill_pending())
                (void)ss_task_sleep(1);
            else
                break;
        if (ss_process_kill_pending())
            return SS_ERR_STATE;
        if (status != SS_OK)
            return status;
        req->exit_status = result;
        return (int32_t)child;
    }
    case SS9_SYS_KILL: {
        SSProcessSnapshot target;
        if (ss_process_snapshot(request, &target) != SS_OK ||
            target.ppid != ss_process_current_id())
            return SS9_ERR;
        return ss_process_kill((SSProcessId)request);
    }
    case SS9_SYS_SLEEP: return (int16_t)ss_task_sleep(request);
    case SS9_SYS_PROC_INFO:
        if (!valid_buffer(request, sizeof(SS9ProcessInfoRequest), 0))
            return SS9_ERR_PARAM;
        {
            const SS9ProcessInfoRequest* req =
                (const SS9ProcessInfoRequest*)(uintptr_t)request;
            return process_info(req->pid, req->info);
        }
    case SS9_SYS_PROC_LIST: {
        if (!valid_buffer(request, sizeof(SS9ProcessListRequest), 1))
            return SS9_ERR_PARAM;
        SS9ProcessListRequest* req = (SS9ProcessListRequest*)(uintptr_t)request;
        SS9ProcessListRequest input = *req;
        SS9ProcessInfo* entries = (SS9ProcessInfo*)(uintptr_t)input.entries;
        if (input.capacity != 0 &&
            !valid_buffer(input.entries,
                          (uint32_t)input.capacity * sizeof(*entries), 1))
            return SS9_ERR_PARAM;
        uint16_t count = 0;
        req->count = 0;
        for (uint16_t i = 0; i < SS_PROCESS_MAX; i++) {
            SSProcessSnapshot snapshot;
            if (ss_process_snapshot_at(i, &snapshot) != SS_OK)
                continue;
            if (count == input.capacity) {
                req->count = count;
                return SS_ERR_LIMIT;
            }
            SS9ProcessInfo* out = &entries[count++];
            out->pid = snapshot.pid;
            out->ppid = snapshot.ppid;
            out->exit_status = snapshot.exit_status;
            out->state = snapshot.state;
            out->reserved = 0;
            for (uint16_t n = 0; n < sizeof(out->name); n++)
                out->name[n] = snapshot.name[n];
        }
        req->count = count;
        return count;
    }
    case SS9_SYS_READ:
    case SS9_SYS_WRITE:
    case SS9_SYS_OPEN:
    case SS9_SYS_CLOSE:
    default: return SS9_ERR_NOSYS;
    }
}
