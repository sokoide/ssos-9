/* Assembler context, symbol table, emission, and relocation helpers. */

#include "asm.h"

#include <stddef.h>

void asm_ctx_init(AsmCtx* ctx) {
    uint32_t i;
    for (i = 0; i < sizeof(*ctx); i++) ((uint8_t*)ctx)[i] = 0;
    ctx->section = ASM_SECTION_TEXT;
}

void asm_set_warn(AsmCtx* ctx, AsmWarnFn fn, void* user) {
    ctx->warn = fn;
    ctx->warn_user = user;
}

const char* asm_status_message(AsmStatus status) {
    switch (status) {
    case ASM_OK: return "ok";
    case ASM_ERR_SYNTAX: return "syntax error";
    case ASM_ERR_LINE_LONG: return "line too long";
    case ASM_ERR_UNKNOWN_MNEMONIC: return "unknown mnemonic";
    case ASM_ERR_UNKNOWN_DIRECTIVE: return "unknown directive";
    case ASM_ERR_UNKNOWN_REGISTER: return "unknown register";
    case ASM_ERR_BAD_SIZE: return "invalid size suffix";
    case ASM_ERR_BAD_OPERAND: return "invalid operand";
    case ASM_ERR_OPERAND_COUNT: return "wrong operand count";
    case ASM_ERR_UNDEFINED_SYMBOL: return "undefined symbol";
    case ASM_ERR_DUPLICATE_SYMBOL: return "duplicate symbol";
    case ASM_ERR_RANGE: return "value out of range";
    case ASM_ERR_TEXT_FULL: return "text section full";
    case ASM_ERR_DATA_FULL: return "data section full";
    case ASM_ERR_BSS_FULL: return "bss section full";
    case ASM_ERR_SYMBOL_TABLE_FULL: return "symbol table full";
    case ASM_ERR_RELOC_TABLE_FULL: return "relocation table full";
    case ASM_ERR_NAME_LONG: return "symbol name too long";
    case ASM_ERR_NOT_ABSOLUTE: return "expression must be absolute";
    case ASM_ERR_WRONG_SECTION: return "invalid section";
    case ASM_ERR_UNSUPPORTED: return "unsupported instruction form";
    case ASM_ERR_WRITE: return "object write failed";
    case ASM_ERR_SOURCE_LONG: return "source too long";
    case ASM_ERR_ODD_OFFSET: return "instruction or word/long data at odd offset";
    }
    return "unknown error";
}

AsmStatus asm_ctx_error(AsmCtx* ctx, AsmStatus status) {
    if (ctx->status == ASM_OK) {
        ctx->status = status;
        ctx->error_line = ctx->line;
    }
    return status;
}

static int name_equal(const AsmSymbol* symbol, const char* name, uint8_t len) {
    uint8_t i;
    for (i = 0; i < len; i++) {
        if (symbol->name[i] != name[i])
            return 0;
    }
    return symbol->name[len] == 0;
}

int asm_sym_lookup(const AsmCtx* ctx, const char* name, uint8_t len) {
    uint32_t i;
    for (i = 0; i < ctx->symbol_count; i++) {
        if (name_equal(&ctx->symbols[i], name, len))
            return (int)i;
    }
    return -1;
}

static AsmStatus sym_create(AsmCtx* ctx, const char* name, uint8_t len,
                            uint8_t section, uint8_t flags, uint32_t value) {
    uint8_t i;
    AsmSymbol* symbol;
    if (len == 0 || len > ASM_NAME_MAX)
        return asm_ctx_error(ctx, ASM_ERR_NAME_LONG);
    if (ctx->symbol_count >= ASM_SYMBOL_MAX)
        return asm_ctx_error(ctx, ASM_ERR_SYMBOL_TABLE_FULL);
    symbol = &ctx->symbols[ctx->symbol_count++];
    for (i = 0; i < len; i++) symbol->name[i] = name[i];
    symbol->name[len] = 0;
    symbol->section = section;
    symbol->flags = flags;
    symbol->def_pass = (uint16_t)(flags & ASM_SYM_EXTERN ? 0u : ctx->pass);
    symbol->value = value;
    return ASM_OK;
}

AsmStatus asm_sym_define(AsmCtx* ctx, const char* name, uint8_t len,
                         uint8_t section, uint32_t value) {
    int existing = asm_sym_lookup(ctx, name, len);
    if (existing >= 0) {
        AsmSymbol* symbol = &ctx->symbols[existing];
        /* A second definition within one pass is always a duplicate, even
         * at the same offset (contract: duplicate symbol). Redefinition
         * across passes is allowed only when both passes define the same
         * label at the same place: identical section and value. */
        if (symbol->flags & ASM_SYM_EXTERN)
            return asm_ctx_error(ctx, ASM_ERR_DUPLICATE_SYMBOL);
        if (symbol->def_pass == ctx->pass)
            return asm_ctx_error(ctx, ASM_ERR_DUPLICATE_SYMBOL);
        if (symbol->section != section || symbol->value != value)
            return asm_ctx_error(ctx, ASM_ERR_DUPLICATE_SYMBOL);
        symbol->def_pass = (uint16_t)ctx->pass;
        return ASM_OK;
    }
    if (section == ASM_SECTION_UNDEF)
        return asm_ctx_error(ctx, ASM_ERR_WRONG_SECTION);
    return sym_create(ctx, name, len, section, 0, value);
}

AsmStatus asm_sym_extern(AsmCtx* ctx, const char* name, uint8_t len) {
    int existing = asm_sym_lookup(ctx, name, len);
    if (existing >= 0) {
        if (ctx->symbols[existing].flags & ASM_SYM_EXTERN)
            return ASM_OK;
        return asm_ctx_error(ctx, ASM_ERR_DUPLICATE_SYMBOL);
    }
    return sym_create(ctx, name, len, ASM_SECTION_UNDEF, ASM_SYM_EXTERN, 0);
}

AsmStatus asm_sym_global(AsmCtx* ctx, const char* name, uint8_t len) {
    int existing = asm_sym_lookup(ctx, name, len);
    if (existing < 0)
        return asm_ctx_error(ctx, ASM_ERR_UNDEFINED_SYMBOL);
    ctx->symbols[existing].flags |= ASM_SYM_GLOBAL;
    return ASM_OK;
}

AsmStatus asm_numeric_define(AsmCtx* ctx, uint8_t digit) {
    if (digit > 9)
        return asm_ctx_error(ctx, ASM_ERR_SYNTAX);
    if (ctx->numeric_count >= ASM_NUMERIC_MAX)
        return asm_ctx_error(ctx, ASM_ERR_SYMBOL_TABLE_FULL);
    ctx->numerics[ctx->numeric_count].digit = digit;
    ctx->numerics[ctx->numeric_count].section = ctx->section;
    ctx->numerics[ctx->numeric_count].line = ctx->line;
    ctx->numerics[ctx->numeric_count].value = ctx->offset[ctx->section];
    ctx->numeric_count++;
    return ASM_OK;
}

int asm_numeric_lookup(const AsmCtx* ctx, uint8_t digit, int forward) {
    uint32_t i;
    int found = -1;
    for (i = 0; i < ctx->numeric_count; i++) {
        if (ctx->numerics[i].digit != digit)
            continue;
        if (forward) {
            if (ctx->numerics[i].line > ctx->line &&
                (found < 0 ||
                 ctx->numerics[i].line < ctx->numerics[found].line))
                found = (int)i;
        } else if (ctx->numerics[i].line <= ctx->line &&
                   (found < 0 ||
                    ctx->numerics[i].line >= ctx->numerics[found].line)) {
            found = (int)i;
        }
    }
    return found;
}

static uint32_t section_offset(const AsmCtx* ctx, uint8_t section) {
    return ctx->offset[section];
}

AsmStatus asm_emit8(AsmCtx* ctx, uint32_t value) {
    uint8_t byte = (uint8_t)(value & 0xFFu);
    return asm_emit_bytes(ctx, &byte, 1);
}

AsmStatus asm_emit16(AsmCtx* ctx, uint32_t value) {
    uint8_t bytes[2];
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
    return asm_emit_bytes(ctx, bytes, 2);
}

AsmStatus asm_emit32(AsmCtx* ctx, uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
    return asm_emit_bytes(ctx, bytes, 4);
}

AsmStatus asm_emit_bytes(AsmCtx* ctx, const uint8_t* bytes, uint32_t count) {
    uint8_t section = ctx->section;
    uint32_t at;
    uint32_t i;
    if (section == ASM_SECTION_TEXT) {
        at = ctx->offset[ASM_SECTION_TEXT];
        if (count > ASM_TEXT_MAX - at)
            return asm_ctx_error(ctx, ASM_ERR_TEXT_FULL);
        for (i = 0; i < count; i++) ctx->text[at + i] = bytes[i];
        ctx->offset[ASM_SECTION_TEXT] = at + count;
        return ASM_OK;
    }
    if (section == ASM_SECTION_DATA) {
        at = ctx->offset[ASM_SECTION_DATA];
        if (count > ASM_DATA_MAX - at)
            return asm_ctx_error(ctx, ASM_ERR_DATA_FULL);
        for (i = 0; i < count; i++) ctx->data[at + i] = bytes[i];
        ctx->offset[ASM_SECTION_DATA] = at + count;
        return ASM_OK;
    }
    return asm_ctx_error(ctx, ASM_ERR_WRONG_SECTION);
}

AsmStatus asm_align_even(AsmCtx* ctx) {
    if (ctx->section == ASM_SECTION_BSS) {
        if (ctx->offset[ASM_SECTION_BSS] & 1u) {
            if (ctx->offset[ASM_SECTION_BSS] + 1 > ASM_BSS_MAX)
                return asm_ctx_error(ctx, ASM_ERR_BSS_FULL);
            ctx->offset[ASM_SECTION_BSS]++;
        }
        return ASM_OK;
    }
    if (section_offset(ctx, ctx->section) & 1u) {
        AsmStatus status = asm_emit8(ctx, 0);
        if (status != ASM_OK)
            return status;
    }
    return ASM_OK;
}

int asm_section_symbol(AsmCtx* ctx, uint8_t section) {
    /* Octal escapes keep the leading control byte exactly one byte wide. */
    static const char names[3][6] = {"\001text", "\001data", "\001bss"};
    static const uint8_t lengths[3] = {5, 5, 4};
    AsmSymbol* symbol;
    uint8_t i;
    int index;
    if (section < ASM_SECTION_TEXT || section > ASM_SECTION_BSS)
        return -1;
    index = asm_sym_lookup(ctx, names[section - 1], lengths[section - 1]);
    if (index >= 0)
        return index;
    if (ctx->symbol_count >= ASM_SYMBOL_MAX)
        return -1;
    symbol = &ctx->symbols[ctx->symbol_count];
    for (i = 0; i < lengths[section - 1]; i++)
        symbol->name[i] = names[section - 1][i];
    symbol->name[lengths[section - 1]] = 0;
    symbol->section = section;
    symbol->flags = ASM_SYM_SECTION;
    symbol->value = 0;
    return (int)ctx->symbol_count++;
}

AsmStatus asm_reloc_add(AsmCtx* ctx, uint8_t section, uint8_t type,
                        uint32_t offset, uint32_t symbol_index,
                        int32_t addend) {
    AsmReloc* reloc;
    if (ctx->reloc_count >= ASM_RELOC_MAX)
        return asm_ctx_error(ctx, ASM_ERR_RELOC_TABLE_FULL);
    reloc = &ctx->relocs[ctx->reloc_count++];
    reloc->section = section;
    reloc->type = type;
    reloc->offset = offset;
    reloc->symbol = symbol_index;
    reloc->addend = addend;
    return ASM_OK;
}
