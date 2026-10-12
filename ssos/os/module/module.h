#ifndef SS_MODULE_H
#define SS_MODULE_H

#include "image.h"

#define SS_MODULE_MAX_MODULES 8u
#define SS_MODULE_NAME_CAPACITY 32u

typedef uint32_t SSModuleId;

typedef enum {
    SS_MODULE_STORAGE_BUILTIN = 1,
    SS_MODULE_STORAGE_DYNAMIC = 2,
} SSModuleStorage;

typedef struct {
    SSModuleId id;
    uint16_t ref_count;
    uint8_t storage;
    char name[SS_MODULE_NAME_CAPACITY];
    SSModuleImageInfo image;
} SSModuleSnapshot;

void ss_module_registry_init(void);
/* BUILTIN bytes must remain immutable and live as long as the registry entry. */
int16_t ss_module_register_image(const uint8_t* bytes, uint32_t size,
                                 SSModuleStorage storage, SSModuleId* id);
int16_t ss_module_find(const char* name, SSModuleId* id);
int16_t ss_module_snapshot(SSModuleId id, SSModuleSnapshot* snapshot);
/* Copies a READY entry without acquiring a reference. Image pointers are
 * diagnostic addresses only; callers must acquire a ref before dereferencing.
 */
int16_t ss_module_snapshot_at(uint16_t index, SSModuleSnapshot* snapshot);
int16_t ss_module_acquire(SSModuleId id, SSModuleSnapshot* snapshot);
int16_t ss_module_release(SSModuleId id);
/* Initializes private data and retains a module reference for the process. */
int16_t ss_module_prepare_data(SSModuleId id, void* data, uint32_t capacity,
                               const void* code_base);
int16_t ss_module_link(SSModuleId id);
int16_t ss_module_unlink(SSModuleId id);
int16_t ss_module_unload(SSModuleId id);

#endif
