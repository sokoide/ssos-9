/* Sequential S9O1 object input with format validation. All reads are
 * strictly sequential (no seek in the storage contract); the file length
 * must match the header fields, checked by exact reads plus lk_reader_end. */

#include "link.h"

#include <stddef.h>

LinkStatus lk_reader_open(LinkReader* reader, const LinkIo* io,
                          const char* path) {
    reader->handle = io->open_read(path);
    reader->fill = 0;
    reader->position = 0;
    if (reader->handle == NULL)
        return LINK_ERR_IO;
    return LINK_OK;
}

/* Refills the chunk buffer. Returns LINK_OK, LINK_ERR_IO, or
 * LINK_ERR_FORMAT when the stream ended early (wanted > 0, got 0). */
static LinkStatus reader_fill(LinkReader* reader, const LinkIo* io) {
    int32_t n =
        io->read(reader->handle, reader->buffer, sizeof(reader->buffer));
    if (n < 0)
        return LINK_ERR_IO;
    if (n == 0)
        return LINK_ERR_FORMAT;
    reader->fill = (uint32_t)n;
    reader->position = 0;
    return LINK_OK;
}

LinkStatus lk_reader_read(LinkReader* reader, const LinkIo* io, void* out,
                          uint32_t length) {
    uint8_t* at = (uint8_t*)out;
    while (length > 0) {
        uint32_t available = reader->fill - reader->position;
        uint32_t take;
        if (available == 0) {
            LinkStatus status = reader_fill(reader, io);
            if (status != LINK_OK)
                return status;
            available = reader->fill - reader->position;
        }
        take = length < available ? length : available;
        {
            uint32_t i;
            for (i = 0; i < take; i++)
                at[i] = reader->buffer[reader->position + i];
        }
        reader->position += take;
        at += take;
        length -= take;
    }
    return LINK_OK;
}

LinkStatus lk_reader_skip(LinkReader* reader, const LinkIo* io,
                          uint32_t length) {
    uint8_t discard[64];
    while (length > 0) {
        uint32_t take = length < sizeof(discard) ? length : sizeof(discard);
        LinkStatus status = lk_reader_read(reader, io, discard, take);
        if (status != LINK_OK)
            return status;
        length -= take;
    }
    return LINK_OK;
}

LinkStatus lk_reader_end(LinkReader* reader, const LinkIo* io) {
    int32_t n;
    if (reader->position < reader->fill)
        return LINK_ERR_FORMAT; /* buffered bytes still pending */
    n = io->read(reader->handle, reader->buffer, 1);
    if (n < 0)
        return LINK_ERR_IO;
    if (n > 0)
        return LINK_ERR_FORMAT; /* trailing data after the object */
    return LINK_OK;
}

void lk_reader_close(LinkReader* reader, const LinkIo* io) {
    (void)io->close(reader->handle);
    reader->handle = NULL;
}

static uint32_t get32(const uint8_t* at) {
    return ((uint32_t)at[0] << 24) | ((uint32_t)at[1] << 16) |
           ((uint32_t)at[2] << 8) | at[3];
}

static uint32_t get16(const uint8_t* at) {
    return ((uint32_t)at[0] << 8) | at[1];
}

LinkStatus lk_obj_header(LinkReader* reader, const LinkIo* io,
                         LinkObjHeader* out) {
    uint8_t raw[LINK_OBJ_HEADER_SIZE];
    uint32_t total;
    LinkStatus status = lk_reader_read(reader, io, raw, LINK_OBJ_HEADER_SIZE);
    if (status != LINK_OK)
        return status;
    if (raw[0] != 'S' || raw[1] != '9' || raw[2] != 'O' || raw[3] != '1')
        return LINK_ERR_FORMAT;
    if (get16(raw + 4) != 1 || get16(raw + 6) != 0)
        return LINK_ERR_FORMAT;
    if (get32(raw + 20) != 0 || get32(raw + 36) != 0)
        return LINK_ERR_FORMAT; /* entry and reserved must be 0 */
    out->text_size = get32(raw + 8);
    out->data_size = get32(raw + 12);
    out->bss_size = get32(raw + 16);
    out->symbol_count = get32(raw + 24);
    out->reloc_count = get32(raw + 28);
    out->pool_size = get32(raw + 32);
    /* Cap each field before summing: a raw uint32 total of unchecked
     * fields wraps (1 + 0xFFFFFFFF == 0) and let a crafted header pass
     * the size budget into OOB writes. Every cap is a cumulative output
     * limit the linker enforces anyway, so an over-cap field is a limit
     * error (docs/linker-contract.md, "領域超過"). */
    if (out->text_size > LINK_TEXT_MAX || out->data_size > LINK_DATA_MAX ||
        out->bss_size > LINK_PRIVATE_MAX ||
        out->symbol_count > LINK_SYMBOL_MAX ||
        out->reloc_count > LINK_RELOC_MAX ||
        out->pool_size > LINK_NAME_POOL_MAX)
        return LINK_ERR_LIMIT;
    /* With every field capped the total is bounded well below 2^32
     * (40 + 32768 + 12288 + 12*512 + 16*4095 + 6144 = 122904), so this
     * sum cannot wrap and stays a plain uint32. */
    total = LINK_OBJ_HEADER_SIZE + out->text_size + out->data_size +
            LINK_OBJ_SYMBOL_SIZE * out->symbol_count +
            LINK_OBJ_RELOC_SIZE * out->reloc_count + out->pool_size;
    if (total > LINK_OBJ_TOTAL_MAX)
        return LINK_ERR_FORMAT;
    return LINK_OK;
}

/* Field width in bytes for a relocation type, or 0 when invalid. */
static uint32_t reloc_width(uint32_t type) {
    switch (type) {
    case LINK_RELOC_PC16:
    case LINK_RELOC_ABS16: return 2;
    case LINK_RELOC_ABS32: return 4;
    case LINK_RELOC_ABS8: return 1;
    }
    return 0;
}

LinkStatus lk_obj_reloc(LinkReader* reader, const LinkIo* io, LinkReloc* out,
                        const LinkObjHeader* header) {
    uint8_t raw[LINK_OBJ_RELOC_SIZE];
    uint32_t section_size;
    uint32_t width;
    LinkStatus status = lk_reader_read(reader, io, raw, LINK_OBJ_RELOC_SIZE);
    if (status != LINK_OK)
        return status;
    out->offset = get32(raw);
    out->symbol = get32(raw + 4);
    out->addend = (int32_t)get32(raw + 8);
    out->section = raw[12];
    out->type = raw[13];
    if (get16(raw + 14) != 0)
        return LINK_ERR_FORMAT;
    if (out->section != LINK_SECTION_TEXT && out->section != LINK_SECTION_DATA)
        return LINK_ERR_FORMAT;
    width = reloc_width(out->type);
    if (width == 0)
        return LINK_ERR_FORMAT;
    if (out->symbol >= header->symbol_count)
        return LINK_ERR_FORMAT;
    section_size = out->section == LINK_SECTION_TEXT ? header->text_size
                                                     : header->data_size;
    if (out->offset >= section_size)
        return LINK_ERR_FORMAT;
    if (out->offset + width > section_size)
        return LINK_ERR_FORMAT;
    return LINK_OK;
}
