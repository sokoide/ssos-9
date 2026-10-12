#include "syscall.h"

#include "../../include/ssos9/syscall.h"
#include "../module/module.h"
#include "../process/process.h"
#include "scheduler.h"

static int32_t process_info(uint32_t pid, uint32_t address) {
    if (address == 0)
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
        if (request == 0)
            return SS9_ERR_PARAM;
        const SS9SpawnRequest* req = (const SS9SpawnRequest*)(uintptr_t)request;
        if (req->reserved != 0)
            return SS9_ERR_PARAM;
        const char* name = (const char*)(uintptr_t)req->name;
        const char* const* argv = (const char* const*)(uintptr_t)req->argv;
        SSModuleId module_id;
        SSProcessId pid;
        int16_t status = ss_module_find(name, &module_id);
        if (status != SS_OK)
            return SS9_ERR;
        status = ss_process_spawn(ss_process_current_id(), module_id, req->argc,
                                  argv, &pid);
        return status == SS_OK ? (int32_t)pid : status;
    }
    case SS9_SYS_EXIT:
        if (ss_process_current_id() == 1)
            return SS9_ERR_PARAM;
        ss_process_exit_current((uint16_t)request);
        ss_task_exit();
    case SS9_SYS_WAIT: {
        if (request == 0)
            return SS9_ERR_PARAM;
        SS9WaitRequest* req = (SS9WaitRequest*)(uintptr_t)request;
        if (req->reserved != 0)
            return SS9_ERR_PARAM;
        uint16_t result;
        int16_t status;
        while ((status = ss_process_wait(ss_process_current_id(), req->pid,
                                         &result)) == SS_ERR_STATE)
            (void)ss_task_sleep(1);
        if (status != SS_OK)
            return status;
        req->exit_status = result;
        return (int32_t)req->pid;
    }
    case SS9_SYS_KILL: {
        SSProcessSnapshot target;
        if (ss_process_snapshot(request, &target) != SS_OK ||
            target.ppid != ss_process_current_id())
            return SS9_ERR;
        return ss_process_kill((SSProcessId)request);
    }
    case SS9_SYS_SLEEP: return ss_task_sleep(request);
    case SS9_SYS_PROC_INFO:
        if (request == 0)
            return SS9_ERR_PARAM;
        {
            const SS9ProcessInfoRequest* req =
                (const SS9ProcessInfoRequest*)(uintptr_t)request;
            return process_info(req->pid, req->info);
        }
    case SS9_SYS_PROC_LIST: {
        if (request == 0)
            return SS9_ERR_PARAM;
        SS9ProcessListRequest* req = (SS9ProcessListRequest*)(uintptr_t)request;
        SS9ProcessInfo* entries = (SS9ProcessInfo*)(uintptr_t)req->entries;
        if (req->capacity != 0 && entries == 0)
            return SS9_ERR_PARAM;
        req->count = 0;
        for (uint16_t i = 0; i < SS_PROCESS_MAX; i++) {
            SSProcessSnapshot snapshot;
            if (ss_process_snapshot_at(i, &snapshot) != SS_OK)
                continue;
            if (req->count == req->capacity)
                return SS_ERR_LIMIT;
            SS9ProcessInfo* out = &entries[req->count++];
            out->pid = snapshot.pid;
            out->ppid = snapshot.ppid;
            out->exit_status = snapshot.exit_status;
            out->state = snapshot.state;
            out->reserved = 0;
            for (uint16_t n = 0; n < sizeof(out->name); n++)
                out->name[n] = snapshot.name[n];
        }
        return req->count;
    }
    case SS9_SYS_READ:
    case SS9_SYS_WRITE:
    case SS9_SYS_OPEN:
    case SS9_SYS_CLOSE:
    default: return SS9_ERR_NOSYS;
    }
}
