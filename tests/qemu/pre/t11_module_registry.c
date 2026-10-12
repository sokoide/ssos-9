/* Exercise module lookup, reference accounting, and unload under QEMU. */

#include "kernel.h"
#include "module.h"
#include "system_arena.h"
#include "tty.h"

#include <stddef.h>

static uint8_t image[136];
static uint8_t conflicting_image[136];

static void put16(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put32(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint32_t get32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t crc32(const uint8_t* bytes, uint32_t size) {
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0; i < size; i++) {
        uint8_t value = i >= 0x34 && i < 0x38 ? 0 : bytes[i];
        crc ^= value;
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0);
    }
    return crc ^ 0xffffffffu;
}

static void set_crc(void) { put32(&image[0x34], crc32(image, sizeof(image))); }

static void make_image(const char* name) {
    for (uint32_t i = 0; i < sizeof(image); i++) image[i] = 0;
    image[0] = 'S';
    image[1] = '9';
    image[2] = 'M';
    image[3] = '1';
    put16(&image[4], 1);
    put16(&image[6], SS_MODULE_IMAGE_HEADER_SIZE);
    put32(&image[8], SS_MODULE_FLAG_PIC | SS_MODULE_FLAG_REENTRANT);
    put32(&image[0x0c], sizeof(image));
    put32(&image[0x10], 80);
    put32(&image[0x14], 8);
    put32(&image[0x18], 80);
    put32(&image[0x1c], 88);
    put32(&image[0x20], 16);
    put32(&image[0x24], 4);
    put32(&image[0x28], 4096);
    put32(&image[0x2c], 104);
    put16(&image[0x30], 7);
    put32(&image[0x38], 0);
    put32(&image[0x3c], 8);
    put32(&image[0x40], 112);
    put32(&image[0x44], 2);
    for (unsigned i = 0; i < 7; i++) image[104 + i] = (uint8_t)name[i];
    image[96] = 0x5a;
    image[97] = 0xa5;
    put32(&image[112], 0);
    put32(&image[116], SS_MODULE_FIXUP_CODE32);
    put32(&image[120], 80);
    put32(&image[124], 4);
    put32(&image[128], SS_MODULE_FIXUP_DATA32);
    put32(&image[132], 8);
    set_crc();
}

static void fail(const char* reason) {
    tty_puts("FAIL ");
    tty_puts(reason);
    tty_puts("\n");
    for (;;) {
    }
}

int main(void) {
    tty_puts("START module-registry\n");
    if (ss_memory_init() != 0)
        fail("memory init");
    uint32_t free_before = ss_memory_free_bytes();
    ss_module_registry_init();

    make_image("counter");
    SSModuleId builtin_id = 0;
    if (ss_module_register_image(image, sizeof(image),
                                 SS_MODULE_STORAGE_BUILTIN,
                                 &builtin_id) != SS_OK ||
        builtin_id == 0)
        fail("register builtin");
    SSModuleId found = 0;
    if (ss_module_find("counter", &found) != SS_OK || found != builtin_id)
        fail("find builtin");
    if (ss_module_link(builtin_id) != SS_OK)
        fail("link builtin");
    SSModuleSnapshot snapshot;
    if (ss_module_snapshot(builtin_id, &snapshot) != SS_OK ||
        snapshot.ref_count != 1 ||
        snapshot.storage != SS_MODULE_STORAGE_BUILTIN)
        fail("builtin snapshot");
    if (ss_module_unlink(builtin_id) != SS_OK ||
        ss_module_unload(builtin_id) == SS_OK)
        fail("builtin lifetime");

    SSModuleId duplicate_id = 0;
    if (ss_module_register_image(image, sizeof(image),
                                 SS_MODULE_STORAGE_BUILTIN,
                                 &duplicate_id) != SS_OK ||
        duplicate_id != builtin_id)
        fail("identical registration");

    for (uint32_t i = 0; i < sizeof(image); i++)
        conflicting_image[i] = image[i];
    conflicting_image[80] ^= 1;
    put32(&conflicting_image[0x34],
          crc32(conflicting_image, sizeof(conflicting_image)));
    if (ss_module_register_image(conflicting_image, sizeof(conflicting_image),
                                 SS_MODULE_STORAGE_BUILTIN,
                                 &duplicate_id) != SS_ERR_STATE)
        fail("conflicting duplicate accepted");

    make_image("dynamic");
    SSModuleId dynamic_id = 0;
    if (ss_module_register_image(image, sizeof(image),
                                 SS_MODULE_STORAGE_DYNAMIC,
                                 &dynamic_id) != SS_OK ||
        dynamic_id == 0)
        fail("register dynamic");
    uint8_t private_data[20];
    for (uint32_t i = 0; i < sizeof(private_data); i++) private_data[i] = 0xff;
    if (ss_module_snapshot(dynamic_id, &snapshot) != SS_OK)
        fail("dynamic snapshot");
    if (ss_module_prepare_data(dynamic_id, private_data, sizeof(private_data),
                               snapshot.image.image + 80) != SS_OK)
        fail("prepare private data");
    if (get32(private_data) !=
            (uint32_t)(uintptr_t)(snapshot.image.image + 80) ||
        get32(private_data + 4) != (uint32_t)(uintptr_t)(private_data + 8) ||
        private_data[8] != 0x5a || private_data[9] != 0xa5 ||
        private_data[16] != 0 || private_data[19] != 0)
        fail("data copy, fixups, or BSS");
    uint8_t original = image[80];
    image[80] ^= 0x5a;
    if (ss_module_snapshot(dynamic_id, &snapshot) != SS_OK ||
        snapshot.storage != SS_MODULE_STORAGE_DYNAMIC ||
        snapshot.image.image[80] != original)
        fail("dynamic image copy");
    if (ss_module_link(dynamic_id) != SS_OK ||
        ss_module_unload(dynamic_id) == SS_OK)
        fail("referenced unload accepted");
    if (ss_module_unlink(dynamic_id) != SS_OK ||
        ss_module_release(dynamic_id) != SS_OK ||
        ss_module_unload(dynamic_id) != SS_OK ||
        ss_memory_free_bytes() != free_before)
        fail("dynamic unload");

    make_image("dynamic");
    SSModuleId reloaded_id = 0;
    if (ss_module_register_image(image, sizeof(image),
                                 SS_MODULE_STORAGE_DYNAMIC,
                                 &reloaded_id) != SS_OK ||
        reloaded_id == dynamic_id)
        fail("generation not advanced");
    if (ss_module_unload(reloaded_id) != SS_OK)
        fail("unload reloaded module");

    tty_puts("OK module refs/unload\n");
    for (;;) {
    }
}
