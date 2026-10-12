#include "image.h"

#include <stddef.h>

#define SS_MODULE_MAX_CODE_SIZE 32768u
#define SS_MODULE_MAX_DATA_SIZE 65536u
#define SS_MODULE_MAX_STACK_SIZE 65532u
#define SS_MODULE_MAX_IMAGE_SIZE 131072u
#define SS_MODULE_FIXUP_SIZE 12u

typedef struct {
    uint32_t offset;
    uint32_t size;
} SSImageRegion;

static uint16_t read_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int range_valid(uint32_t offset, uint32_t size, uint32_t limit) {
    return offset <= limit && size <= limit - offset;
}

static int regions_overlap(SSImageRegion a, SSImageRegion b) {
    if (a.size == 0 || b.size == 0)
        return 0;
    return a.offset < b.offset + b.size && b.offset < a.offset + a.size;
}

static int is_zero(const uint8_t* p, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        if (p[i] != 0)
            return 0;
    }
    return 1;
}

static int valid_name_char(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

uint32_t ss_module_image_crc32(const uint8_t* image, uint32_t size) {
    if (image == NULL || size < SS_MODULE_IMAGE_HEADER_SIZE)
        return 0;
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0; i < size; i++) {
        uint8_t value = (i >= 0x34 && i < 0x38) ? 0 : image[i];
        crc ^= value;
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0);
    }
    return crc ^ 0xffffffffu;
}

static int validate_fixups(const uint8_t* image, uint32_t fixup_offset,
                           uint32_t fixup_count, uint32_t init_size,
                           uint32_t data_size, uint32_t code_offset,
                           uint32_t code_size) {
    for (uint32_t i = 0; i < fixup_count; i++) {
        const uint8_t* record = image + fixup_offset + i * SS_MODULE_FIXUP_SIZE;
        uint32_t target = read_be32(record);
        uint32_t kind = read_be32(record + 4);
        uint32_t value = read_be32(record + 8);
        if ((target & 3u) != 0 || !range_valid(target, 4, init_size))
            return -1;

        if (kind == SS_MODULE_FIXUP_CODE32) {
            if (value < code_offset || value >= code_offset + code_size)
                return -1;
        } else if (kind == SS_MODULE_FIXUP_DATA32) {
            if (value >= data_size)
                return -1;
        } else {
            return -1;
        }

        for (uint32_t j = 0; j < i; j++) {
            const uint8_t* previous =
                image + fixup_offset + j * SS_MODULE_FIXUP_SIZE;
            if (read_be32(previous) == target)
                return -1;
        }
    }
    return 0;
}

int16_t ss_module_image_validate(const uint8_t* image, uint32_t size,
                                 SSModuleImageInfo* info) {
    if (image == NULL || info == NULL || size < SS_MODULE_IMAGE_HEADER_SIZE ||
        size > SS_MODULE_MAX_IMAGE_SIZE)
        return -1;
    if (image[0] != 'S' || image[1] != '9' || image[2] != 'M' ||
        image[3] != '1' || read_be16(image + 4) != 1 ||
        read_be16(image + 6) != SS_MODULE_IMAGE_HEADER_SIZE)
        return -1;

    uint32_t flags = read_be32(image + 8);
    uint32_t image_size = read_be32(image + 0x0c);
    uint32_t code_offset = read_be32(image + 0x10);
    uint32_t code_size = read_be32(image + 0x14);
    uint32_t entry_offset = read_be32(image + 0x18);
    uint32_t data_offset = read_be32(image + 0x1c);
    uint32_t init_size = read_be32(image + 0x20);
    uint32_t bss_size = read_be32(image + 0x24);
    uint32_t stack_size = read_be32(image + 0x28);
    uint32_t name_offset = read_be32(image + 0x2c);
    uint16_t name_size = read_be16(image + 0x30);
    uint32_t got_offset = read_be32(image + 0x38);
    uint32_t got_size = read_be32(image + 0x3c);
    uint32_t fixup_offset = read_be32(image + 0x40);
    uint32_t fixup_count = read_be32(image + 0x44);
    uint32_t crc = read_be32(image + 0x34);

    if (flags != (SS_MODULE_FLAG_PIC | SS_MODULE_FLAG_REENTRANT) ||
        image_size != size || read_be16(image + 0x32) != 0 ||
        !is_zero(image + 0x48, 8) || ss_module_image_crc32(image, size) != crc)
        return -1;
    if (code_size == 0 || code_size > SS_MODULE_MAX_CODE_SIZE ||
        code_offset < SS_MODULE_IMAGE_HEADER_SIZE || (code_offset & 1u) != 0 ||
        !range_valid(code_offset, code_size, size) ||
        (entry_offset & 1u) != 0 || entry_offset < code_offset ||
        entry_offset >= code_offset + code_size)
        return -1;
    if (init_size > SS_MODULE_MAX_DATA_SIZE ||
        bss_size > SS_MODULE_MAX_DATA_SIZE - init_size || stack_size < 4096 ||
        stack_size > SS_MODULE_MAX_STACK_SIZE || (stack_size & 3u) != 0 ||
        data_offset < SS_MODULE_IMAGE_HEADER_SIZE ||
        !range_valid(data_offset, init_size, size))
        return -1;
    uint32_t data_size = init_size + bss_size;
    if (got_size == 0 || (got_offset & 3u) != 0 || (got_size & 3u) != 0 ||
        !range_valid(got_offset, got_size, data_size))
        return -1;
    if (name_size == 0 || name_size > 31 ||
        name_offset < SS_MODULE_IMAGE_HEADER_SIZE ||
        !range_valid(name_offset, name_size, size))
        return -1;
    for (uint16_t i = 0; i < name_size; i++) {
        if (!valid_name_char(image[name_offset + i]))
            return -1;
    }
    if (image[name_offset] == '.' || image[name_offset + name_size - 1] == '.')
        return -1;
    if (fixup_count > SS_MODULE_MAX_IMAGE_SIZE / SS_MODULE_FIXUP_SIZE ||
        fixup_count > UINT32_MAX / SS_MODULE_FIXUP_SIZE)
        return -1;
    uint32_t fixup_size = fixup_count * SS_MODULE_FIXUP_SIZE;
    if ((fixup_count != 0 && fixup_offset < SS_MODULE_IMAGE_HEADER_SIZE) ||
        !range_valid(fixup_offset, fixup_size, size))
        return -1;

    SSImageRegion regions[] = {
        {0, SS_MODULE_IMAGE_HEADER_SIZE}, {code_offset, code_size},
        {data_offset, init_size},         {name_offset, name_size},
        {fixup_offset, fixup_size},
    };
    for (uint32_t i = 0; i < sizeof(regions) / sizeof(regions[0]); i++) {
        for (uint32_t j = i + 1; j < sizeof(regions) / sizeof(regions[0]);
             j++) {
            if (regions_overlap(regions[i], regions[j]))
                return -1;
        }
    }
    if (validate_fixups(image, fixup_offset, fixup_count, init_size, data_size,
                        code_offset, code_size) != 0)
        return -1;

    info->image = image;
    info->image_size = image_size;
    info->flags = flags;
    info->code_offset = code_offset;
    info->code_size = code_size;
    info->entry_offset = entry_offset;
    info->data_offset = data_offset;
    info->init_size = init_size;
    info->bss_size = bss_size;
    info->stack_size = stack_size;
    info->name_offset = name_offset;
    info->name_size = name_size;
    info->got_offset = got_offset;
    info->got_size = got_size;
    info->fixup_offset = fixup_offset;
    info->fixup_count = fixup_count;
    return 0;
}
