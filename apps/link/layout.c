/* Pass 1: scan input objects, build the global symbol table and name
 * pool, assign section bases, and check duplicate/undefined globals and
 * capacity limits. Relocations are format-validated and discarded here;
 * pass 2 re-reads them. */

#include "link.h"

/* Returns the status unchanged; the caller records the error detail
 * before the final return (fail never clobbers a recorded symbol or
 * relocation diagnostic). */
static LinkStatus fail(LinkLayout* layout, LinkStatus status) {
    (void)layout;
    return status;
}

/* Length of the NUL-terminated pool name at offset (bounded by the pool). */
uint32_t lk_name_length(const LinkLayout* layout, uint32_t offset) {
    uint32_t length = 0;
    while (offset + length < layout->pool_size &&
           layout->pool[offset + length] != 0)
        length++;
    return length;
}

static LinkStatus fail_symbol(LinkLayout* layout, LinkStatus status,
                              const LinkSymbol* symbol) {
    layout->error.kind = 1;
    layout->error.name_offset = symbol->name;
    layout->error.name_len = lk_name_length(layout, symbol->name);
    return status;
}

void lk_layout_init(LinkLayout* layout, uint32_t object_count) {
    layout->object_count = object_count;
    layout->symbol_count = 0;
    layout->pool_size = 0;
    layout->text_total = 0;
    layout->data_total = 0;
    layout->bss_total = 0;
    layout->error.kind = 0;
}

/* Compares the pool name at offset with the pool name [name_offset,
 * name_offset + length) — both live in the same pool. */
static int name_equal(const LinkLayout* layout, uint32_t offset,
                      uint32_t name_offset, uint32_t length) {
    uint32_t i;
    if (lk_name_length(layout, offset) != length)
        return 0;
    for (i = 0; i < length; i++) {
        if (layout->pool[offset + i] != layout->pool[name_offset + i])
            return 0;
    }
    return 1;
}

static uint32_t get32(const uint8_t* at) {
    return ((uint32_t)at[0] << 24) | ((uint32_t)at[1] << 16) |
           ((uint32_t)at[2] << 8) | at[3];
}

static uint32_t get16(const uint8_t* at) {
    return ((uint32_t)at[0] << 8) | at[1];
}

/* Reads one symbol record and validates its fields. The name offset is
 * checked after the pool has been loaded (the pool trails the symbols in
 * the file). */
static LinkStatus read_symbol(LinkReader* reader, const LinkIo* io,
                              const LinkObjHeader* header, LinkSymbol* out) {
    uint8_t raw[LINK_OBJ_SYMBOL_SIZE];
    uint32_t section;
    LinkStatus status = lk_reader_read(reader, io, raw, LINK_OBJ_SYMBOL_SIZE);
    if (status != LINK_OK)
        return status;
    out->name = get32(raw);
    out->section = raw[4];
    out->flags = raw[5];
    if (get16(raw + 6) != 0)
        return LINK_ERR_FORMAT;
    out->value = get32(raw + 8);
    if (out->flags > 0x07u)
        return LINK_ERR_FORMAT;
    section = out->section;
    if (section == LINK_SECTION_TEXT) {
        /* value == size is a valid end-of-section label. */
        if (out->value > header->text_size)
            return LINK_ERR_FORMAT;
    } else if (section == LINK_SECTION_DATA) {
        if (out->value > header->data_size)
            return LINK_ERR_FORMAT;
    } else if (section == LINK_SECTION_BSS) {
        if (out->value > header->bss_size)
            return LINK_ERR_FORMAT;
    } else if (section != LINK_SECTION_ABS && section != LINK_SECTION_UNDEF) {
        return LINK_ERR_FORMAT;
    }
    return LINK_OK;
}

/* Pass-1 scan of one input object. */
LinkStatus lk_layout_input(LinkLayout* layout, const LinkIo* io,
                           const char* path, uint32_t object) {
    LinkReader reader;
    LinkObjHeader header;
    LinkObject* ledger = &layout->objects[object];
    layout->error.kind = 0; /* plain diagnostics unless a detail is set */
    uint32_t pool_base = layout->pool_size;
    uint32_t symbol_base = layout->symbol_count;
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

    /* Section bases: text concatenates in input order with a 0x00 pad
     * before this object when the previous text ends on an odd offset;
     * private data is [all data][all bss], no padding. */
    if (object > 0 && (layout->text_total & 1u) != 0)
        layout->text_total++;
    ledger->text_base = layout->text_total;
    ledger->data_base = layout->data_total;
    ledger->bss_base = layout->bss_total; /* running; resolve adds data total */
    ledger->text_size = header.text_size;
    ledger->data_size = header.data_size;
    ledger->bss_size = header.bss_size;
    ledger->symbol_base = symbol_base;
    ledger->symbol_count = header.symbol_count;
    ledger->reloc_count = header.reloc_count;
    ledger->pool_size = header.pool_size;

    /* Capacity limits use the same init size the output will: the dummy
     * GOT pads init data to 4 bytes even when all inputs have no data,
     * and the loader rejects init + bss over LINK_PRIVATE_MAX. */
    {
        uint32_t init = layout->data_total + header.data_size;
        if (init < 4u)
            init = 4u;
        if (layout->text_total + header.text_size > LINK_TEXT_MAX ||
            layout->data_total + header.data_size > LINK_DATA_MAX ||
            init + layout->bss_total + header.bss_size > LINK_PRIVATE_MAX) {
            lk_reader_close(&reader, io);
            return fail(layout, LINK_ERR_LIMIT);
        }
    }
    if (layout->symbol_count + header.symbol_count > LINK_SYMBOL_MAX ||
        pool_base + header.pool_size > LINK_NAME_POOL_MAX) {
        lk_reader_close(&reader, io);
        return fail(layout, LINK_ERR_LIMIT);
    }

    /* text and data bytes are not retained between inputs; discard them. */
    status = lk_reader_skip(&reader, io, header.text_size);
    if (status == LINK_OK)
        status = lk_reader_skip(&reader, io, header.data_size);

    /* Symbols: validate fields now, resolve names after the pool loads. */
    for (i = 0; status == LINK_OK && i < header.symbol_count; i++) {
        LinkSymbol* symbol = &layout->symbols[symbol_base + i];
        status = read_symbol(&reader, io, &header, symbol);
        symbol->object = (uint16_t)object;
    }

    /* Relocations: read and validate, then discard (pass 2 re-reads). */
    for (i = 0; status == LINK_OK && i < header.reloc_count; i++) {
        LinkReloc reloc;
        status = lk_obj_reloc(&reader, io, &reloc, &header);
    }

    /* Name pool: append this object's pool verbatim (no de-duplication);
     * symbol name offsets shift by the pool base. */
    for (i = 0; status == LINK_OK && i < header.pool_size; i += 256) {
        uint32_t take = header.pool_size - i;
        if (take > 256)
            take = 256;
        status = lk_reader_read(&reader, io,
                                (uint8_t*)layout->pool + pool_base + i, take);
    }
    if (status == LINK_OK) {
        layout->pool_size = pool_base + header.pool_size;
        for (i = 0; i < header.symbol_count; i++) {
            LinkSymbol* symbol = &layout->symbols[symbol_base + i];
            /* Check the object-relative offset before rebasing it (a
             * huge value would wrap), then require the NUL to sit inside
             * this object's pool slice. */
            if (symbol->name >= header.pool_size) {
                status = LINK_ERR_FORMAT;
                break;
            }
            symbol->name = pool_base + symbol->name;
            if (symbol->name + lk_name_length(layout, symbol->name) >=
                layout->pool_size) {
                status = LINK_ERR_FORMAT; /* name misses its NUL */
                break;
            }
        }
    }

    /* Duplicate global definitions: a name defined global twice anywhere
     * is an error. Compare against every earlier global symbol. */
    for (i = 0; status == LINK_OK && i < header.symbol_count; i++) {
        LinkSymbol* symbol = &layout->symbols[symbol_base + i];
        uint32_t length;
        uint32_t j;
        if ((symbol->flags & LINK_SYM_GLOBAL) == 0)
            continue;
        length = lk_name_length(layout, symbol->name);
        for (j = 0; j < symbol_base + i; j++) {
            const LinkSymbol* other = &layout->symbols[j];
            if ((other->flags & LINK_SYM_GLOBAL) == 0)
                continue;
            if (name_equal(layout, other->name, symbol->name, length)) {
                status = fail_symbol(layout, LINK_ERR_DUPLICATE, symbol);
                break;
            }
        }
    }

    if (status == LINK_OK)
        status = lk_reader_end(&reader, io);

    /* Commit the layout totals only on success; the caller stops at the
     * first error, so partial state is never used. */
    if (status == LINK_OK) {
        layout->text_total += header.text_size;
        layout->data_total += header.data_size;
        layout->bss_total += header.bss_size;
        layout->symbol_count = symbol_base + header.symbol_count;
    }
    lk_reader_close(&reader, io);
    if (status != LINK_OK)
        return fail(layout, status);
    return LINK_OK;
}

/* Finds a global definition of the pool name [name_offset, name_offset +
 * length). object filters to one input, UINT32_MAX matches any. Returns
 * the symbol index or -1. */
static int find_global(const LinkLayout* layout, uint32_t name_offset,
                       uint32_t length, uint32_t object) {
    uint32_t i;
    for (i = 0; i < layout->symbol_count; i++) {
        const LinkSymbol* symbol = &layout->symbols[i];
        if ((symbol->flags & LINK_SYM_GLOBAL) == 0 ||
            symbol->section == LINK_SECTION_UNDEF)
            continue;
        if (object != UINT32_MAX && symbol->object != object)
            continue;
        if (name_equal(layout, symbol->name, name_offset, length))
            return (int)i;
    }
    return -1;
}

/* Resolves every symbol in place to output-space values. */
LinkStatus lk_layout_resolve(LinkLayout* layout) {
    uint32_t i;
    for (i = 0; i < layout->object_count; i++)
        layout->objects[i].bss_base += layout->data_total;
    for (i = 0; i < layout->symbol_count; i++) {
        LinkSymbol* symbol = &layout->symbols[i];
        const LinkObject* owner = &layout->objects[symbol->object];
        if (symbol->section == LINK_SECTION_UNDEF) {
            uint32_t length = lk_name_length(layout, symbol->name);
            int def = find_global(layout, symbol->name, length, symbol->object);
            if (def < 0)
                def = find_global(layout, symbol->name, length, UINT32_MAX);
            if (def < 0) {
                layout->error.kind = 1;
                layout->error.name_offset = symbol->name;
                layout->error.name_len = length;
                return LINK_ERR_UNDEFINED;
            }
            symbol->section = layout->symbols[def].section;
            symbol->value = layout->symbols[def].value;
            owner = &layout->objects[layout->symbols[def].object];
        }
        switch (symbol->section) {
        case LINK_SECTION_TEXT:
            symbol->value = owner->text_base + symbol->value;
            break;
        case LINK_SECTION_DATA:
            symbol->value = owner->data_base + symbol->value;
            break;
        case LINK_SECTION_BSS:
            symbol->value = owner->bss_base + symbol->value;
            break;
        default: /* abs: keep the constant */ break;
        }
    }
    return LINK_OK;
}
