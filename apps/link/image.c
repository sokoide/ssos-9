/* Output S9M1 image: geometry, header, CRC32, module name derivation,
 * and diagnostic text. The image regions (header, code, init data, name,
 * fixups) are contiguous in the byte buffer, so the finished image goes
 * out with a single ss9_file_store. */

#include "link.h"

void lk_image_init(LinkImage* image) {
    uint32_t i;
    for (i = 0; i < LINK_IMAGE_HEADER_SIZE; i++) image->bytes[i] = 0;
    image->text_size = 0;
    image->code_size = 0;
    image->data_size = 0;
    image->init_size = 4;
    image->bss_size = 0;
    image->name_size = 0;
    image->data_offset = 0;
    image->name_offset = 0;
    image->fixup_offset = 0;
    image->fixup_count = 0;
    image->image_size = 0;
}

/* Computes the image geometry from the pass-1 totals. */
LinkStatus lk_image_plan(LinkImage* image, const LinkLayout* layout,
                         uint32_t name_size) {
    image->text_size = layout->text_total;
    image->code_size = (image->text_size + 3u) & ~3u;
    image->data_size = layout->data_total;
    image->init_size =
        image->data_size < 4u ? 4u : image->data_size; /* dummy GOT space */
    image->bss_size = layout->bss_total;
    image->name_size = name_size;
    image->data_offset = LINK_IMAGE_HEADER_SIZE + image->code_size;
    image->name_offset = image->data_offset + image->init_size;
    image->fixup_offset = image->name_offset + name_size;
    image->fixup_count = 0;
    image->image_size = 0;
    if (image->text_size == 0)
        return LINK_ERR_EMPTY_TEXT; /* entry is text[0] */
    /* State every region bound explicitly instead of relying on the
     * component caps happening to sum below the buffer: code by
     * LINK_TEXT_MAX, init data by LINK_DATA_MAX (pass 1 caps
     * data_total), the name by the module name rule, and the fixup
     * region by its own worst case 12 * LINK_FIXUP_MAX. With all
     * components capped, data_offset + init_size cannot wrap and the
     * worst legal geometry (80 + 32768 + 12288 + 31 + 3072 = 48239)
     * still fits LINK_IMAGE_BUF_MAX. */
    if (image->code_size > LINK_TEXT_MAX ||
        image->init_size > LINK_DATA_MAX ||
        image->name_size > LINK_MODULE_NAME_MAX ||
        image->data_offset + image->init_size > LINK_IMAGE_BUF_MAX ||
        image->fixup_offset + 12u * LINK_FIXUP_MAX > LINK_IMAGE_BUF_MAX) {
        return LINK_ERR_LIMIT;
    }
    return LINK_OK;
}

/* IEEE CRC32 (reflected polynomial 0xEDB88320, init/final XOR
 * 0xFFFFFFFF), two 4-bit table steps per byte. The 16-entry table is
 * built on the stack per call: the S9M1 code-only profile has no static
 * writable data, and a 1 KB byte-index table would eat a third of the
 * 32768-byte .text budget as rodata. The result is bit-identical to the
 * bit-serial version (same polynomial per step) and matches zlib
 * crc32. */
static uint32_t crc32_add(uint32_t crc, const uint8_t* data, uint32_t length) {
    uint32_t table[16];
    uint32_t i;
    uint32_t n;
    for (n = 0; n < 16; n++) {
        uint32_t value = n;
        int bit;
        for (bit = 0; bit < 4; bit++) {
            uint32_t mask = 0u - (value & 1u);
            value = (value >> 1) ^ (0xEDB88320u & mask);
        }
        table[n] = value;
    }
    for (i = 0; i < length; i++) {
        crc ^= data[i];
        crc = (crc >> 4) ^ table[crc & 0xFu];
        crc = (crc >> 4) ^ table[crc & 0xFu];
    }
    return crc;
}

static void put16(uint8_t* at, uint32_t value) {
    at[0] = (uint8_t)(value >> 8);
    at[1] = (uint8_t)value;
}

static void put32(uint8_t* at, uint32_t value) {
    at[0] = (uint8_t)(value >> 24);
    at[1] = (uint8_t)(value >> 16);
    at[2] = (uint8_t)(value >> 8);
    at[3] = (uint8_t)value;
}

LinkStatus lk_image_finish(LinkImage* image, const char* name,
                           uint32_t name_len) {
    uint32_t crc;
    uint32_t i;
    /* Zero the code padding and the dummy GOT padding below data_size. */
    for (i = image->text_size; i < image->code_size; i++)
        image->bytes[LINK_IMAGE_HEADER_SIZE + i] = 0;
    for (i = image->data_size; i < image->init_size; i++)
        image->bytes[image->data_offset + i] = 0;
    for (i = 0; i < name_len; i++)
        image->bytes[image->name_offset + i] = (uint8_t)name[i];

    image->image_size = image->fixup_offset + 12u * image->fixup_count;
    if (image->image_size > LINK_IMAGE_MAX ||
        image->image_size > LINK_IMAGE_BUF_MAX)
        return LINK_ERR_LIMIT;

    put32(image->bytes + 0x00, 0x53394D31u); /* "S9M1" */
    put16(image->bytes + 0x04, 1);           /* ABI version */
    put16(image->bytes + 0x06, LINK_IMAGE_HEADER_SIZE);
    put32(image->bytes + 0x08, 0x3u); /* PIC | REENTRANT */
    put32(image->bytes + 0x0c, image->image_size);
    put32(image->bytes + 0x10, LINK_IMAGE_HEADER_SIZE); /* code offset */
    put32(image->bytes + 0x14, image->code_size);
    put32(image->bytes + 0x18, LINK_IMAGE_HEADER_SIZE); /* entry: text[0] */
    put32(image->bytes + 0x1c, image->data_offset);
    put32(image->bytes + 0x20, image->init_size);
    put32(image->bytes + 0x24, image->bss_size);
    put32(image->bytes + 0x28, 4096u); /* stack size */
    put32(image->bytes + 0x2c, image->name_offset);
    put16(image->bytes + 0x30, (uint16_t)image->name_size);
    put16(image->bytes + 0x32, 0); /* reserved */
    put32(image->bytes + 0x34, 0); /* CRC field is zero during the pass */
    put32(image->bytes + 0x38, 0); /* dummy GOT: offset 0 */
    put32(image->bytes + 0x3c, 4); /* dummy GOT: size 4 */
    put32(image->bytes + 0x40, image->fixup_offset);
    put32(image->bytes + 0x44, image->fixup_count);
    put32(image->bytes + 0x48, 0);
    put32(image->bytes + 0x4c, 0);

    crc = crc32_add(0xFFFFFFFFu, image->bytes, image->image_size);
    put32(image->bytes + 0x34, crc ^ 0xFFFFFFFFu);
    return LINK_OK;
}

/* Derives the module name from the output file name: the base before the
 * last extension separator, validated against the loader rule
 * ([A-Za-z0-9_.-], no leading or trailing dot, 1..31 bytes). */
LinkStatus lk_module_name(const char* output, char* out) {
    uint32_t length = 0;
    uint32_t base = 0;
    uint32_t i;
    while (output[length] != 0 && length < 64u) length++;
    if (length == 0 || length >= 64u)
        return LINK_ERR_ARG;
    for (i = 0; i < length; i++) {
        if (output[i] == '.')
            base = i;
    }
    if (base == 0)
        base = length; /* no extension: keep the whole name */
    if (base > LINK_MODULE_NAME_MAX || base == 0)
        return LINK_ERR_ARG;
    for (i = 0; i < base; i++) {
        char c = output[i];
        int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok)
            return LINK_ERR_ARG;
        out[i] = c;
    }
    if (out[0] == '.' || out[base - 1u] == '.')
        return LINK_ERR_ARG;
    out[base] = 0;
    return LINK_OK;
}

/* --- diagnostics --- */

const char* lk_status_message(LinkStatus status) {
    switch (status) {
    case LINK_OK: return "OK";
    case LINK_ERR_ARG: return "Argument error";
    case LINK_ERR_FORMAT: return "Invalid object format";
    case LINK_ERR_UNDEFINED: return "Undefined symbol";
    case LINK_ERR_DUPLICATE: return "Duplicate symbol";
    case LINK_ERR_PIC: return "PIC violation";
    case LINK_ERR_ALIGN: return "Fixup target is not 4-byte aligned";
    case LINK_ERR_DOUBLE_FIXUP: return "Duplicate fixup target";
    case LINK_ERR_RANGE: return "Relocation value out of range";
    case LINK_ERR_FIXUP:
        return "Relocation cannot be expressed as a data fixup";
    case LINK_ERR_LIMIT: return "Limit exceeded";
    case LINK_ERR_EMPTY_TEXT: return "Empty text section";
    case LINK_ERR_CHANGED: return "Object changed between passes";
    case LINK_ERR_IO: return "File I/O error";
    }
    return "Internal error";
}

/* Appends source and NUL-terminates. Returns the new length, clipped at
 * capacity - 1. */
static uint32_t append_text(char* out, uint32_t capacity, uint32_t length,
                            const char* text) {
    while (*text != 0 && length + 1u < capacity) out[length++] = *text++;
    if (capacity > 0)
        out[length < capacity ? length : capacity - 1u] = 0;
    return length;
}

static uint32_t append_number(char* out, uint32_t capacity, uint32_t length,
                              uint32_t value) {
    char digits[10];
    uint32_t n = 0;
    if (value == 0)
        digits[n++] = '0';
    while (value != 0) {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (n > 0) {
        char c = digits[--n];
        if (length + 1u >= capacity)
            break;
        out[length++] = c;
    }
    if (capacity > 0)
        out[length < capacity ? length : capacity - 1u] = 0;
    return length;
}

static const char* reloc_type_name(uint32_t type) {
    switch (type) {
    case LINK_RELOC_PC16: return "PC16";
    case LINK_RELOC_ABS32: return "ABS32";
    case LINK_RELOC_ABS16: return "ABS16";
    case LINK_RELOC_ABS8: return "ABS8";
    }
    return "?";
}

static const char* section_name(uint32_t section) {
    switch (section) {
    case LINK_SECTION_ABS: return "abs";
    case LINK_SECTION_TEXT: return "text";
    case LINK_SECTION_DATA: return "data";
    case LINK_SECTION_BSS: return "bss";
    }
    return "?";
}

LinkStatus lk_error_message(LinkStatus status, const LinkError* error,
                            const LinkLayout* layout, char* out,
                            uint32_t capacity) {
    uint32_t length = 0;
    length = append_text(out, capacity, length, lk_status_message(status));
    if (error->kind == 1) {
        uint32_t i;
        length = append_text(out, capacity, length, " ");
        for (i = 0; i < error->name_len && length + 1u < capacity; i++)
            out[length++] = layout->pool[error->name_offset + i];
        if (capacity > 0)
            out[length] = 0;
        return status;
    }
    if (error->kind == 2) {
        length = append_text(out, capacity, length, ": ");
        length = append_text(out, capacity, length,
                             reloc_type_name(error->reloc_type));
        length = append_text(out, capacity, length, " in ");
        length = append_text(out, capacity, length,
                             section_name(error->place_section));
        length = append_text(out, capacity, length, " at ");
        length = append_number(out, capacity, length, error->offset);
        length = append_text(out, capacity, length, " targets ");
        length = append_text(out, capacity, length,
                             section_name(error->target_section));
        return status;
    }
    return status;
}
