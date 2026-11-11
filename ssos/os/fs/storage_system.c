#include "scsi_volume.h"
#include "../app/cli_console.h"

SSScsiVolume ss_system_scsi_volume;

#define SS_FILE_HANDLES 16u
#define SS_FILE_FD_FIRST 3u
#define SS_FILE_FD_LIMIT 11u

typedef struct {
    SSFat12File file;
    uint32_t owner;
    uint8_t fd;
} SSFileHandle;

static SSFileHandle handles[SS_FILE_HANDLES];

int32_t ss_system_open_reader(uint32_t owner, const char* name) {
    if (!owner) return -1;
    ss_iocs_lock();
    uint32_t slot, fd;
    for (slot = 0; slot < SS_FILE_HANDLES && handles[slot].owner; slot++) {}
    if (slot == SS_FILE_HANDLES) { ss_iocs_unlock(); return -1; }
    for (fd = SS_FILE_FD_FIRST; fd < SS_FILE_FD_LIMIT; fd++) {
        uint32_t i;
        for (i = 0; i < SS_FILE_HANDLES; i++)
            if (handles[i].owner == owner && handles[i].fd == fd) break;
        if (i == SS_FILE_HANDLES) break;
    }
    if (fd == SS_FILE_FD_LIMIT ||
        ss_storage_open_reader(&ss_system_scsi_volume.storage, name, &handles[slot].file)) {
        ss_iocs_unlock();
        return -1;
    }
    handles[slot].owner = owner;
    handles[slot].fd = (uint8_t)fd;
    ss_iocs_unlock();
    return (int32_t)fd;
}

int32_t ss_system_read_fd(uint32_t owner, uint32_t fd, uint8_t* data, uint32_t size) {
    if (!owner) return -1;
    ss_iocs_lock();
    int32_t result = -1;
    for (uint32_t i = 0; i < SS_FILE_HANDLES; i++)
        if (handles[i].owner == owner && handles[i].fd == fd) {
            result = ss_fat12_read(&handles[i].file, data, size);
            break;
        }
    ss_iocs_unlock();
    return result;
}

int32_t ss_system_close_fd(uint32_t owner, uint32_t fd) {
    if (!owner) return -1;
    ss_iocs_lock();
    int32_t result = -1;
    for (uint32_t i = 0; i < SS_FILE_HANDLES; i++)
        if (handles[i].owner == owner && handles[i].fd == fd) {
            ss_storage_close_reader(&ss_system_scsi_volume.storage, &handles[i].file);
            handles[i].owner = 0;
            result = 0;
            break;
        }
    ss_iocs_unlock();
    return result;
}

void ss_system_close_process_files(uint32_t owner) {
    if (!owner) return;
    ss_iocs_lock();
    for (uint32_t i = 0; i < SS_FILE_HANDLES; i++)
        if (handles[i].owner == owner) {
            ss_storage_close_reader(&ss_system_scsi_volume.storage, &handles[i].file);
            handles[i].owner = 0;
        }
    ss_iocs_unlock();
}

int32_t ss_system_file_load(const char* name, uint8_t* data, uint32_t capacity) {
    ss_iocs_lock();
    int32_t result = ss_storage_read(&ss_system_scsi_volume.storage, name,
                                     data, capacity);
    ss_iocs_unlock();
    return result;
}

int32_t ss_system_file_store(const char* name, const uint8_t* data,
                             uint32_t size, uint32_t replace) {
    ss_iocs_lock();
    int result = ss_storage_store(&ss_system_scsi_volume.storage, name, data,
                                  size, (int)replace);
    ss_iocs_unlock();
    return result == 0 ? (int32_t)size : -1;
}
