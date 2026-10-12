#include "builtin.h"

#include "image.h"
#include "module.h"

#define COUNTER_IMAGE_SIZE 124u
#define COUNTER_CODE_OFFSET 80u
#define COUNTER_DATA_OFFSET 88u
#define COUNTER_NAME_OFFSET 104u
#define COUNTER_FIXUP_OFFSET 112u

static uint8_t counter_image[COUNTER_IMAGE_SIZE];

static void write_be16(uint32_t offset, uint16_t value) {
    counter_image[offset] = (uint8_t)(value >> 8);
    counter_image[offset + 1] = (uint8_t)value;
}

static void write_be32(uint32_t offset, uint32_t value) {
    counter_image[offset] = (uint8_t)(value >> 24);
    counter_image[offset + 1] = (uint8_t)(value >> 16);
    counter_image[offset + 2] = (uint8_t)(value >> 8);
    counter_image[offset + 3] = (uint8_t)value;
}

static void make_counter_image(void) {
    for (uint32_t i = 0; i < sizeof(counter_image); i++) counter_image[i] = 0;

    counter_image[0] = 'S';
    counter_image[1] = '9';
    counter_image[2] = 'M';
    counter_image[3] = '1';
    write_be16(4, 1);
    write_be16(6, SS_MODULE_IMAGE_HEADER_SIZE);
    write_be32(8, SS_MODULE_FLAG_PIC | SS_MODULE_FLAG_REENTRANT);
    write_be32(0x0c, sizeof(counter_image));
    write_be32(0x10, COUNTER_CODE_OFFSET);
    write_be32(0x14, 8);
    write_be32(0x18, COUNTER_CODE_OFFSET);
    write_be32(0x1c, COUNTER_DATA_OFFSET);
    write_be32(0x20, 16);
    write_be32(0x24, 4);
    write_be32(0x28, 4096);
    write_be32(0x2c, COUNTER_NAME_OFFSET);
    write_be16(0x30, 7);
    write_be32(0x38, 0);
    write_be32(0x3c, 4);
    write_be32(0x40, COUNTER_FIXUP_OFFSET);
    write_be32(0x44, 1);

    /* movea.l (a5),a0; addq.l #1,(a0); bra.s to the first instruction. */
    counter_image[COUNTER_CODE_OFFSET] = 0x20;
    counter_image[COUNTER_CODE_OFFSET + 1] = 0x55;
    counter_image[COUNTER_CODE_OFFSET + 2] = 0x52;
    counter_image[COUNTER_CODE_OFFSET + 3] = 0x90;
    counter_image[COUNTER_CODE_OFFSET + 4] = 0x60;
    counter_image[COUNTER_CODE_OFFSET + 5] = 0xfa;
    counter_image[COUNTER_NAME_OFFSET] = 'c';
    counter_image[COUNTER_NAME_OFFSET + 1] = 'o';
    counter_image[COUNTER_NAME_OFFSET + 2] = 'u';
    counter_image[COUNTER_NAME_OFFSET + 3] = 'n';
    counter_image[COUNTER_NAME_OFFSET + 4] = 't';
    counter_image[COUNTER_NAME_OFFSET + 5] = 'e';
    counter_image[COUNTER_NAME_OFFSET + 6] = 'r';
    write_be32(COUNTER_FIXUP_OFFSET, 0);
    write_be32(COUNTER_FIXUP_OFFSET + 4, SS_MODULE_FIXUP_DATA32);
    write_be32(COUNTER_FIXUP_OFFSET + 8, 8);
    write_be32(0x34,
               ss_module_image_crc32(counter_image, sizeof(counter_image)));
}

int16_t ss_builtin_modules_init(void) {
    make_counter_image();
    SSModuleId id;
    return ss_module_register_image(counter_image, sizeof(counter_image),
                                    SS_MODULE_STORAGE_BUILTIN, &id);
}
