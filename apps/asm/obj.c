/* S9O1 object serialization. See docs/assembler-contract.md for the
 * authoritative layout; all multi-byte integers are big-endian. */

#include "asm.h"

/* Whole-object size limit from the S9O1 contract. */
#define ASM_OBJ_MAX_TOTAL 65535u

static AsmStatus sink_put(AsmObjSink sink, void* user, const void* bytes,
                          uint32_t length) {
    if (length == 0)
        return ASM_OK;
    if (sink(bytes, length, user) != 0)
        return ASM_ERR_WRITE;
    return ASM_OK;
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

static uint32_t sym_name_len(const AsmSymbol* symbol) {
    uint32_t len = 0;
    while (len < ASM_NAME_MAX && symbol->name[len] != 0) len++;
    return len;
}

AsmStatus asm_obj_write(const AsmCtx* ctx, AsmObjSink sink, void* user) {
    uint8_t rec[64];
    uint32_t text_size = ctx->offset[ASM_SECTION_TEXT];
    uint32_t data_size = ctx->offset[ASM_SECTION_DATA];
    uint32_t pool_size = 0;
    uint32_t name_offset;
    uint32_t total;
    uint32_t i;
    AsmStatus status;

    for (i = 0; i < ctx->symbol_count; i++) {
        pool_size += sym_name_len(&ctx->symbols[i]) + 1;
    }
    total = ASM_OBJ_HEADER_SIZE + text_size + data_size +
            ASM_OBJ_SYMBOL_SIZE * ctx->symbol_count +
            ASM_OBJ_RELOC_SIZE * ctx->reloc_count + pool_size;
    if (total > ASM_OBJ_MAX_TOTAL)
        return ASM_ERR_RANGE;

    rec[0] = 'S';
    rec[1] = '9';
    rec[2] = 'O';
    rec[3] = '1';
    put16(rec + 4, 1);
    put16(rec + 6, 0);
    put32(rec + 8, text_size);
    put32(rec + 12, data_size);
    put32(rec + 16, ctx->bss_size);
    put32(rec + 20, 0);
    put32(rec + 24, ctx->symbol_count);
    put32(rec + 28, ctx->reloc_count);
    put32(rec + 32, pool_size);
    put32(rec + 36, 0);
    status = sink_put(sink, user, rec, ASM_OBJ_HEADER_SIZE);
    if (status != ASM_OK)
        return status;

    status = sink_put(sink, user, ctx->text, text_size);
    if (status != ASM_OK)
        return status;
    status = sink_put(sink, user, ctx->data, data_size);
    if (status != ASM_OK)
        return status;

    name_offset = 0;
    for (i = 0; i < ctx->symbol_count; i++) {
        const AsmSymbol* symbol = &ctx->symbols[i];
        put32(rec, name_offset);
        rec[4] = symbol->section;
        rec[5] = symbol->flags;
        put16(rec + 6, 0);
        put32(rec + 8, symbol->value);
        status = sink_put(sink, user, rec, ASM_OBJ_SYMBOL_SIZE);
        if (status != ASM_OK)
            return status;
        name_offset += sym_name_len(symbol) + 1;
    }

    for (i = 0; i < ctx->reloc_count; i++) {
        const AsmReloc* reloc = &ctx->relocs[i];
        put32(rec, reloc->offset);
        put32(rec + 4, reloc->symbol);
        put32(rec + 8, (uint32_t)reloc->addend);
        rec[12] = reloc->section;
        rec[13] = reloc->type;
        put16(rec + 14, 0);
        status = sink_put(sink, user, rec, ASM_OBJ_RELOC_SIZE);
        if (status != ASM_OK)
            return status;
    }

    for (i = 0; i < ctx->symbol_count; i++) {
        const AsmSymbol* symbol = &ctx->symbols[i];
        rec[0] = 0;
        status = sink_put(sink, user, symbol->name, sym_name_len(symbol));
        if (status != ASM_OK)
            return status;
        status = sink_put(sink, user, rec, 1);
        if (status != ASM_OK)
            return status;
    }
    return ASM_OK;
}
