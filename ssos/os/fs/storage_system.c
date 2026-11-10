#include "scsi_volume.h"
#include "../app/cli_console.h"

SSScsiVolume ss_system_scsi_volume;

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
