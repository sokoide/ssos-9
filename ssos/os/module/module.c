#include "module.h"

#include "../kernel/kernel.h"
#include "../mem/system_arena.h"

#include <stddef.h>

typedef enum {
    MODULE_FREE = 0,
    MODULE_LOADING,
    MODULE_READY,
} SSModuleState;

typedef struct {
    SSModuleImageInfo image;
    uint8_t* bytes;
    uint32_t generation;
    uint16_t ref_count;
    uint8_t storage;
    uint8_t state;
    char name[SS_MODULE_NAME_CAPACITY];
} SSModuleEntry;

static SSModuleEntry module_table[SS_MODULE_MAX_MODULES];

static void copy_bytes(void* destination, const void* source, uint32_t size) {
    uint8_t* out = destination;
    const uint8_t* in = source;
    for (uint32_t i = 0; i < size; i++) out[i] = in[i];
}

static int names_equal(const char* left, const char* right) {
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

static uint32_t image_name_length(const SSModuleImageInfo* image) {
    return image->name_size;
}

static uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void write_be32(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static int module_name_equal(const SSModuleEntry* entry, const char* name) {
    return entry->state != MODULE_FREE && names_equal(entry->name, name);
}

static SSModuleId make_id(uint32_t generation, uint32_t slot) {
    return (generation << 8) | (slot + 1u);
}

static SSModuleEntry* lookup_locked(SSModuleId id) {
    uint32_t slot_code = id & 0xffu;
    uint32_t generation = id >> 8;
    if (slot_code == 0 || slot_code > SS_MODULE_MAX_MODULES || generation == 0)
        return NULL;
    SSModuleEntry* entry = &module_table[slot_code - 1u];
    if (entry->state != MODULE_READY || entry->generation != generation)
        return NULL;
    return entry;
}

static void fill_snapshot(const SSModuleEntry* entry,
                          SSModuleSnapshot* output) {
    output->id = make_id(entry->generation, (uint32_t)(entry - module_table));
    output->ref_count = entry->ref_count;
    output->storage = entry->storage;
    for (uint32_t i = 0; i < SS_MODULE_NAME_CAPACITY; i++)
        output->name[i] = entry->name[i];
    output->image = entry->image;
}

void ss_module_registry_init(void) {
    uint16_t saved_sr = ss_irq_save();
    for (uint32_t i = 0; i < SS_MODULE_MAX_MODULES; i++) {
        uint32_t generation = module_table[i].generation;
        for (uint32_t j = 0; j < sizeof(module_table[i]); j++)
            ((uint8_t*)&module_table[i])[j] = 0;
        module_table[i].generation = generation;
    }
    ss_irq_restore(saved_sr);
}

int16_t ss_module_register_image(const uint8_t* bytes, uint32_t size,
                                 SSModuleStorage storage, SSModuleId* id) {
    if (bytes == NULL || id == NULL ||
        (storage != SS_MODULE_STORAGE_BUILTIN &&
         storage != SS_MODULE_STORAGE_DYNAMIC))
        return SS_ERR_PARAM;

    SSModuleImageInfo image_info;
    if (ss_module_image_validate(bytes, size, &image_info) != 0)
        return SS_ERR_PARAM;

    char name[SS_MODULE_NAME_CAPACITY];
    uint32_t name_size = image_name_length(&image_info);
    for (uint32_t i = 0; i < name_size; i++)
        name[i] = (char)bytes[image_info.name_offset + i];
    name[name_size] = '\0';

    uint16_t saved_sr = ss_irq_save();
    uint32_t free_slot = SS_MODULE_MAX_MODULES;
    SSModuleEntry* existing = NULL;
    for (uint32_t i = 0; i < SS_MODULE_MAX_MODULES; i++) {
        SSModuleEntry* entry = &module_table[i];
        if (entry->state == MODULE_FREE) {
            if (free_slot == SS_MODULE_MAX_MODULES)
                free_slot = i;
            continue;
        }
        if (!module_name_equal(entry, name))
            continue;
        if (entry->state != MODULE_READY) {
            ss_irq_restore(saved_sr);
            return SS_ERR_STATE;
        }
        if (entry->image.image_size != size) {
            ss_irq_restore(saved_sr);
            return SS_ERR_STATE;
        }
        if (entry->ref_count == UINT16_MAX) {
            ss_irq_restore(saved_sr);
            return SS_ERR_LIMIT;
        }
        entry->ref_count++;
        existing = entry;
        SSModuleId existing_id = make_id(entry->generation, i);
        ss_irq_restore(saved_sr);
        int identical = 1;
        for (uint32_t j = 0; j < size; j++) {
            if (existing->bytes[j] != bytes[j]) {
                identical = 0;
                break;
            }
        }
        saved_sr = ss_irq_save();
        existing->ref_count--;
        ss_irq_restore(saved_sr);
        if (!identical)
            return SS_ERR_STATE;
        *id = existing_id;
        return SS_OK;
    }
    if (free_slot == SS_MODULE_MAX_MODULES) {
        ss_irq_restore(saved_sr);
        return SS_ERR_LIMIT;
    }

    SSModuleEntry* reserved = &module_table[free_slot];
    uint32_t generation = (reserved->generation + 1u) & 0x00ffffffu;
    if (generation == 0)
        generation = 1;
    reserved->generation = generation;
    reserved->state = MODULE_LOADING;
    reserved->storage = (uint8_t)storage;
    for (uint32_t i = 0; i < SS_MODULE_NAME_CAPACITY; i++)
        reserved->name[i] = name[i];
    SSModuleId reserved_id = make_id(generation, free_slot);
    ss_irq_restore(saved_sr);

    uint8_t* stored = (uint8_t*)bytes;
    if (storage == SS_MODULE_STORAGE_DYNAMIC) {
        stored = ss_memory_alloc(size);
        if (stored == NULL) {
            saved_sr = ss_irq_save();
            reserved->state = MODULE_FREE;
            ss_irq_restore(saved_sr);
            return SS_ERR_LIMIT;
        }
        copy_bytes(stored, bytes, size);
    }
    image_info.image = stored;

    saved_sr = ss_irq_save();
    reserved->bytes = stored;
    reserved->image = image_info;
    reserved->ref_count = 0;
    reserved->state = MODULE_READY;
    *id = reserved_id;
    ss_irq_restore(saved_sr);
    return SS_OK;
}

int16_t ss_module_find(const char* name, SSModuleId* id) {
    if (name == NULL || id == NULL)
        return SS_ERR_PARAM;
    uint16_t saved_sr = ss_irq_save();
    for (uint32_t i = 0; i < SS_MODULE_MAX_MODULES; i++) {
        SSModuleEntry* entry = &module_table[i];
        if (entry->state == MODULE_LOADING && names_equal(entry->name, name)) {
            ss_irq_restore(saved_sr);
            return SS_ERR_STATE;
        }
        if (entry->state == MODULE_READY && names_equal(entry->name, name)) {
            *id = make_id(entry->generation, i);
            ss_irq_restore(saved_sr);
            return SS_OK;
        }
    }
    ss_irq_restore(saved_sr);
    return SS_ERR_ID;
}

int16_t ss_module_snapshot(SSModuleId id, SSModuleSnapshot* snapshot) {
    if (snapshot == NULL)
        return SS_ERR_PARAM;
    uint16_t saved_sr = ss_irq_save();
    SSModuleEntry* entry = lookup_locked(id);
    if (entry == NULL) {
        ss_irq_restore(saved_sr);
        return SS_ERR_ID;
    }
    fill_snapshot(entry, snapshot);
    ss_irq_restore(saved_sr);
    return SS_OK;
}

int16_t ss_module_acquire(SSModuleId id, SSModuleSnapshot* snapshot) {
    if (snapshot == NULL)
        return SS_ERR_PARAM;
    uint16_t saved_sr = ss_irq_save();
    SSModuleEntry* entry = lookup_locked(id);
    if (entry == NULL) {
        ss_irq_restore(saved_sr);
        return SS_ERR_ID;
    }
    if (entry->ref_count == UINT16_MAX) {
        ss_irq_restore(saved_sr);
        return SS_ERR_LIMIT;
    }
    entry->ref_count++;
    fill_snapshot(entry, snapshot);
    ss_irq_restore(saved_sr);
    return SS_OK;
}

int16_t ss_module_release(SSModuleId id) {
    uint16_t saved_sr = ss_irq_save();
    SSModuleEntry* entry = lookup_locked(id);
    if (entry == NULL) {
        ss_irq_restore(saved_sr);
        return SS_ERR_ID;
    }
    if (entry->ref_count == 0) {
        ss_irq_restore(saved_sr);
        return SS_ERR_STATE;
    }
    entry->ref_count--;
    ss_irq_restore(saved_sr);
    return SS_OK;
}

int16_t ss_module_prepare_data(SSModuleId id, void* data, uint32_t capacity,
                               const void* code_base) {
    if (data == NULL || code_base == NULL)
        return SS_ERR_PARAM;

    SSModuleSnapshot module;
    int16_t status = ss_module_acquire(id, &module);
    if (status != SS_OK)
        return status;

    uint32_t data_size = module.image.init_size + module.image.bss_size;
    if (capacity < data_size) {
        ss_module_release(id);
        return SS_ERR_PARAM;
    }

    uint8_t* destination = data;
    const uint8_t* image = module.image.image;
    copy_bytes(destination, image + module.image.data_offset,
               module.image.init_size);
    for (uint32_t i = module.image.init_size; i < data_size; i++)
        destination[i] = 0;

    uint32_t data_address = (uint32_t)(uintptr_t)data;
    uint32_t code_address = (uint32_t)(uintptr_t)code_base;
    for (uint32_t i = 0; i < module.image.fixup_count; i++) {
        const uint8_t* record = image + module.image.fixup_offset + i * 12u;
        uint32_t target = read_be32(record);
        uint32_t kind = read_be32(record + 4);
        uint32_t value = read_be32(record + 8);
        uint32_t offset = kind == SS_MODULE_FIXUP_CODE32
                              ? value - module.image.code_offset
                              : value;
        uint32_t base =
            kind == SS_MODULE_FIXUP_CODE32 ? code_address : data_address;
        if (UINT32_MAX - base < offset) {
            ss_module_release(id);
            return SS_ERR_PARAM;
        }
        uint32_t address = base + offset;
        write_be32(destination + target, address);
    }
    return SS_OK;
}

int16_t ss_module_link(SSModuleId id) {
    SSModuleSnapshot ignored;
    return ss_module_acquire(id, &ignored);
}

int16_t ss_module_unlink(SSModuleId id) { return ss_module_release(id); }

int16_t ss_module_unload(SSModuleId id) {
    uint16_t saved_sr = ss_irq_save();
    SSModuleEntry* entry = lookup_locked(id);
    if (entry == NULL) {
        ss_irq_restore(saved_sr);
        return SS_ERR_ID;
    }
    if (entry->storage != SS_MODULE_STORAGE_DYNAMIC || entry->ref_count != 0) {
        ss_irq_restore(saved_sr);
        return SS_ERR_STATE;
    }
    uint8_t* bytes = entry->bytes;
    uint32_t generation = entry->generation;
    for (uint32_t i = 0; i < sizeof(*entry); i++) ((uint8_t*)entry)[i] = 0;
    entry->generation = generation;
    ss_irq_restore(saved_sr);
    return ss_memory_free(bytes);
}
