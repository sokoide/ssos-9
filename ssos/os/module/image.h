#ifndef SS_MODULE_IMAGE_H
#define SS_MODULE_IMAGE_H

#include <stdint.h>

#define SS_MODULE_IMAGE_HEADER_SIZE 80u
#define SS_MODULE_FLAG_PIC 0x00000001u
#define SS_MODULE_FLAG_REENTRANT 0x00000002u
#define SS_MODULE_FIXUP_CODE32 1u
#define SS_MODULE_FIXUP_DATA32 2u

typedef struct {
    const uint8_t* image;
    uint32_t image_size;
    uint32_t flags;
    uint32_t code_offset;
    uint32_t code_size;
    uint32_t entry_offset;
    uint32_t data_offset;
    uint32_t init_size;
    uint32_t bss_size;
    uint32_t stack_size;
    uint32_t name_offset;
    uint16_t name_size;
    uint32_t got_offset;
    uint32_t got_size;
    uint32_t fixup_offset;
    uint32_t fixup_count;
} SSModuleImageInfo;

int16_t ss_module_image_validate(const uint8_t* image, uint32_t size,
                                 SSModuleImageInfo* info);
uint32_t ss_module_image_crc32(const uint8_t* image, uint32_t size);

#endif
