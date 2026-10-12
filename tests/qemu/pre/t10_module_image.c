/* Validate the production S9M1 header and fixup table parser under QEMU. */

#include "image.h"
#include "tty.h"

#include <stddef.h>

static uint8_t image[124];

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

static void make_valid_image(void) {
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
    put32(&image[0x3c], 4);
    put32(&image[0x40], 112);
    put32(&image[0x44], 1);
    image[104] = 'c';
    image[105] = 'o';
    image[106] = 'u';
    image[107] = 'n';
    image[108] = 't';
    image[109] = 'e';
    image[110] = 'r';
    put32(&image[112], 4);
    put32(&image[116], SS_MODULE_FIXUP_DATA32);
    put32(&image[120], 8);
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
    SSModuleImageInfo info;
    tty_puts("START module-image\n");
    make_valid_image();
    if (ss_module_image_validate(image, sizeof(image), &info) != 0)
        fail("valid image");
    if (info.entry_offset != 80 || info.init_size != 16 ||
        info.fixup_count != 1 || info.name_size != 7)
        fail("parsed fields");

    image[110] ^= 1;
    if (ss_module_image_validate(image, sizeof(image), &info) == 0)
        fail("CRC corruption accepted");

    make_valid_image();
    put16(&image[6], 64);
    set_crc();
    if (ss_module_image_validate(image, sizeof(image), &info) == 0)
        fail("bad header size accepted");

    make_valid_image();
    put32(&image[116], 99);
    set_crc();
    if (ss_module_image_validate(image, sizeof(image), &info) == 0)
        fail("unknown fixup accepted");

    make_valid_image();
    put32(&image[0x10], 0xfffffffcu);
    set_crc();
    if (ss_module_image_validate(image, sizeof(image), &info) == 0)
        fail("overflowed code range accepted");

    make_valid_image();
    put32(&image[0x1c], 80);
    set_crc();
    if (ss_module_image_validate(image, sizeof(image), &info) == 0)
        fail("overlapping code and data accepted");

    tty_puts("OK S9M1 validation\n");
    for (;;) {
    }
}
