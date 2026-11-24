/* Pass 2: copy object sections into the output image and apply the
 * relocations, per the contract's application table (docs/linker-contract.md,
 * "再配置の適用"). Values are always recomputed as S + A - P (PC16) or
 * S + A (ABS); the object's field values are never trusted. Data ABS32
 * fields targeting text/data/bss generate CODE32/DATA32 fixups. */

#include "link.h"

static LinkStatus fail(LinkLayout* layout, LinkStatus status) {
    (void)layout;
    return status;
}

/* Detail for rejections that happen before a resolved symbol exists
 * (bound checks); the target section renders as "?". */
static LinkStatus fail_reloc(LinkLayout* layout, LinkStatus status,
                             const LinkReloc* reloc, uint32_t offset) {
    layout->error.kind = 2;
    layout->error.reloc_type = reloc->type;
    layout->error.place_section = reloc->section;
    layout->error.target_section = 0xFFu;
    layout->error.offset = offset;
    return status;
}

static LinkStatus fail_reloc_target(LinkLayout* layout, LinkStatus status,
                                    const LinkReloc* reloc,
                                    const LinkSymbol* symbol, uint32_t offset) {
    layout->error.kind = 2;
    layout->error.reloc_type = reloc->type;
    layout->error.place_section = reloc->section;
    layout->error.target_section = symbol->section;
    layout->error.offset = offset;
    return status;
}

void lk_put8(uint8_t* at, uint32_t value) { at[0] = (uint8_t)value; }

void lk_put16(uint8_t* at, uint32_t value) {
    at[0] = (uint8_t)(value >> 8);
    at[1] = (uint8_t)value;
}

void lk_put32(uint8_t* at, uint32_t value) {
    at[0] = (uint8_t)(value >> 24);
    at[1] = (uint8_t)(value >> 16);
    at[2] = (uint8_t)(value >> 8);
    at[3] = (uint8_t)value;
}

static uint32_t get32(const uint8_t* at) {
    return ((uint32_t)at[0] << 24) | ((uint32_t)at[1] << 16) |
           ((uint32_t)at[2] << 8) | at[3];
}

/* Signed 16-bit range: -32768..32767. */
static int fits_signed16(uint32_t value) {
    return value <= 0x7FFFu || value >= 0xFFFF8000u;
}

/* asm .word fold range: -32768..65535. */
static int fits_d16_or_u16(uint32_t value) {
    return value <= 0xFFFFu || value >= 0xFFFF8000u;
}

/* asm .byte fold range: -128..255. */
static int fits_byte(uint32_t value) {
    return value <= 0xFFu || value >= 0xFFFFFF80u;
}

/* Appends a fixup record after checking the slot alignment, the init
 * data bound, and uniqueness of the target. Returns LINK_OK, or the
 * corresponding diagnostic. */
static LinkStatus add_fixup(LinkImage* image, LinkLayout* layout,
                            const LinkReloc* reloc, const LinkSymbol* symbol,
                            uint32_t target, uint32_t kind, uint32_t value) {
    uint32_t i;
    if ((target & 3u) != 0)
        return fail_reloc_target(layout, LINK_ERR_ALIGN, reloc, symbol, target);
    if (target >= image->init_size)
        return fail_reloc_target(layout, LINK_ERR_RANGE, reloc, symbol, target);
    for (i = 0; i < image->fixup_count; i++) {
        if (get32(image->bytes + image->fixup_offset + i * 12u) == target)
            return fail_reloc_target(layout, LINK_ERR_DOUBLE_FIXUP, reloc,
                                     symbol, target);
    }
    if (image->fixup_count >= LINK_FIXUP_MAX)
        return fail_reloc_target(layout, LINK_ERR_LIMIT, reloc, symbol, target);
    lk_put32(image->bytes + image->fixup_offset + image->fixup_count * 12u,
             target);
    lk_put32(image->bytes + image->fixup_offset + image->fixup_count * 12u + 4u,
             kind);
    lk_put32(image->bytes + image->fixup_offset + image->fixup_count * 12u + 8u,
             value);
    image->fixup_count++;
    return LINK_OK;
}

/* Applies one relocation of one input object. */
static LinkStatus apply_reloc(LinkImage* image, LinkLayout* layout,
                              uint32_t object, const LinkReloc* reloc) {
    const LinkObject* ledger = &layout->objects[object];
    const LinkSymbol* symbol;
    uint32_t target_section;
    uint32_t source; /* output-space resolved value */
    uint32_t addend = (uint32_t)reloc->addend;
    uint32_t value;
    uint8_t* at;

    if (reloc->symbol >= ledger->symbol_count)
        return fail_reloc(layout, LINK_ERR_FORMAT, reloc, reloc->offset);
    symbol = &layout->symbols[ledger->symbol_base + reloc->symbol];
    target_section = symbol->section;
    source = symbol->value;

    if (reloc->section == LINK_SECTION_TEXT) {
        /* P is the extended-word address of the field in the output text. */
        uint32_t place = ledger->text_base + reloc->offset;
        at = image->bytes + LINK_IMAGE_HEADER_SIZE + place;
        switch (reloc->type) {
        case LINK_RELOC_PC16:
            if (target_section != LINK_SECTION_TEXT)
                return fail_reloc_target(layout, LINK_ERR_PIC, reloc, symbol,
                                         reloc->offset);
            value = source + addend - place;
            if (!fits_signed16(value))
                return fail_reloc_target(layout, LINK_ERR_RANGE, reloc, symbol,
                                         reloc->offset);
            lk_put16(at, value);
            return LINK_OK;
        case LINK_RELOC_ABS32:
            /* Fixups cannot patch code: nothing absolute may live in text. */
            return fail_reloc_target(layout, LINK_ERR_PIC, reloc, symbol,
                                     reloc->offset);
        case LINK_RELOC_ABS16:
            if (target_section == LINK_SECTION_DATA ||
                target_section == LINK_SECTION_BSS) {
                /* Always read as an A5 displacement (private data offset). */
                value = source + addend;
                if (!fits_signed16(value))
                    return fail_reloc_target(layout, LINK_ERR_RANGE, reloc,
                                             symbol, reloc->offset);
                lk_put16(at, value);
                return LINK_OK;
            }
            if (target_section == LINK_SECTION_ABS) {
                value = source + addend;
                if (!fits_d16_or_u16(value))
                    return fail_reloc_target(layout, LINK_ERR_RANGE, reloc,
                                             symbol, reloc->offset);
                lk_put16(at, value);
                return LINK_OK;
            }
            return fail_reloc_target(layout, LINK_ERR_PIC, reloc, symbol,
                                     reloc->offset);
        default: /* LINK_RELOC_ABS8 */
            if (target_section != LINK_SECTION_ABS)
                return fail_reloc_target(layout, LINK_ERR_PIC, reloc, symbol,
                                         reloc->offset);
            value = source + addend;
            if (!fits_byte(value))
                return fail_reloc_target(layout, LINK_ERR_RANGE, reloc, symbol,
                                         reloc->offset);
            lk_put8(at, value);
            return LINK_OK;
        }
    }

    /* Relocations placed in init data. Field position is a private data
     * offset; the init data region starts at image data_offset. */
    at = image->bytes + image->data_offset + ledger->data_base + reloc->offset;
    switch (reloc->type) {
    case LINK_RELOC_PC16:
        return fail_reloc_target(layout, LINK_ERR_PIC, reloc, symbol,
                                 reloc->offset);
    case LINK_RELOC_ABS32:
        if (target_section == LINK_SECTION_ABS) {
            lk_put32(at, source + addend); /* constant: no fixup needed */
            return LINK_OK;
        }
        /* The fixup payload is the offset the loader adds to the section
         * base, so the addend folds into it: CODE32 carries
         * header + S + A (the loader adds the code address and drops the
         * code offset), DATA32 carries S + A. A payload outside the
         * target section is rejected by the module validator at load. */
        if (target_section == LINK_SECTION_TEXT)
            return add_fixup(
                image, layout, reloc, symbol, ledger->data_base + reloc->offset,
                LINK_FIXUP_CODE32,
                LINK_IMAGE_HEADER_SIZE + source + (uint32_t)addend);
        return add_fixup(image, layout, reloc, symbol,
                         ledger->data_base + reloc->offset, LINK_FIXUP_DATA32,
                         source + (uint32_t)addend);
    case LINK_RELOC_ABS16:
        if (target_section != LINK_SECTION_ABS)
            return fail_reloc_target(layout, LINK_ERR_FIXUP, reloc, symbol,
                                     reloc->offset);
        value = source + addend;
        if (!fits_d16_or_u16(value))
            return fail_reloc_target(layout, LINK_ERR_RANGE, reloc, symbol,
                                     reloc->offset);
        lk_put16(at, value);
        return LINK_OK;
    default: /* LINK_RELOC_ABS8 */
        if (target_section != LINK_SECTION_ABS)
            return fail_reloc_target(layout, LINK_ERR_FIXUP, reloc, symbol,
                                     reloc->offset);
        value = source + addend;
        if (!fits_byte(value))
            return fail_reloc_target(layout, LINK_ERR_RANGE, reloc, symbol,
                                     reloc->offset);
        lk_put8(at, value);
        return LINK_OK;
    }
}

/* Pass-2 fold of one input object. */
LinkStatus lk_fold_input(LinkImage* image, LinkLayout* layout, const LinkIo* io,
                         const char* path, uint32_t object) {
    LinkReader reader;
    LinkObjHeader header;
    const LinkObject* ledger = &layout->objects[object];
    uint32_t i;
    LinkStatus status;

    status = lk_reader_open(&reader, io, path);
    if (status != LINK_OK)
        return fail(layout, status);
    status = lk_obj_header(&reader, io, &header);
    if (status != LINK_OK) {
        lk_reader_close(&reader, io);
        return fail(layout, status);
    }

    /* The file may have changed between the two passes: reconcile the
     * re-read header against the pass-1 ledger before writing anything,
     * so pass-2 sizes can never exceed the limits layout validated. */
    if (header.text_size != ledger->text_size ||
        header.data_size != ledger->data_size ||
        header.bss_size != ledger->bss_size ||
        header.symbol_count != ledger->symbol_count ||
        header.reloc_count != ledger->reloc_count ||
        header.pool_size != ledger->pool_size) {
        lk_reader_close(&reader, io);
        layout->error.kind = 0;
        return LINK_ERR_CHANGED;
    }

    /* 0x00 pad before this object when the previous text ended odd. */
    if (object > 0) {
        const LinkObject* previous = &layout->objects[object - 1];
        if (((previous->text_base + previous->text_size) & 1u) != 0)
            image->bytes[LINK_IMAGE_HEADER_SIZE + ledger->text_base - 1u] = 0;
    }

    /* Copy text and data into the output image regions. */
    status = lk_reader_read(
        &reader, io, image->bytes + LINK_IMAGE_HEADER_SIZE + ledger->text_base,
        header.text_size);
    if (status == LINK_OK)
        status = lk_reader_read(
            &reader, io, image->bytes + image->data_offset + ledger->data_base,
            header.data_size);
    if (status == LINK_OK)
        status = lk_reader_skip(&reader, io,
                                LINK_OBJ_SYMBOL_SIZE * header.symbol_count);

    /* Apply relocations as they stream past. */
    for (i = 0; status == LINK_OK && i < header.reloc_count; i++) {
        LinkReloc reloc;
        status = lk_obj_reloc(&reader, io, &reloc, &header);
        if (status == LINK_OK)
            status = apply_reloc(image, layout, object, &reloc);
    }

    if (status == LINK_OK)
        status = lk_reader_skip(&reader, io, header.pool_size);
    if (status == LINK_OK)
        status = lk_reader_end(&reader, io);
    lk_reader_close(&reader, io);
    if (status != LINK_OK)
        return fail(layout, status);
    return LINK_OK;
}
