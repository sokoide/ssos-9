/* Two-pass assembly driver core: statement execution shared by the S9M1
 * module CLI and the host test driver. */

#include "asm.h"

static uint32_t section_cap(uint8_t section) {
    if (section == ASM_SECTION_TEXT)
        return ASM_TEXT_MAX;
    if (section == ASM_SECTION_DATA)
        return ASM_DATA_MAX;
    if (section == ASM_SECTION_BSS)
        return ASM_BSS_MAX;
    return 0;
}

static AsmStatus section_full_error(AsmCtx* ctx, uint8_t section) {
    if (section == ASM_SECTION_TEXT)
        return asm_ctx_error(ctx, ASM_ERR_TEXT_FULL);
    if (section == ASM_SECTION_DATA)
        return asm_ctx_error(ctx, ASM_ERR_DATA_FULL);
    return asm_ctx_error(ctx, ASM_ERR_BSS_FULL);
}

/* Pass 1 offset advance with capacity check. */
static AsmStatus advance(AsmCtx* ctx, uint32_t bytes) {
    uint8_t section = ctx->section;
    uint32_t cap = section_cap(section);
    if (cap == 0)
        return asm_ctx_error(ctx, ASM_ERR_WRONG_SECTION);
    if (bytes > cap - ctx->offset[section])
        return section_full_error(ctx, section);
    ctx->offset[section] += bytes;
    return ASM_OK;
}

static AsmStatus define_label(AsmCtx* ctx, const AsmStmt* stmt) {
    if (stmt->label_len == 0)
        return ASM_OK;
    if (stmt->label_len == 1 && stmt->label[0] >= '0' &&
        stmt->label[0] <= '9') {
        if (ctx->pass != 1)
            return ASM_OK;
        return asm_numeric_define(ctx, (uint8_t)(stmt->label[0] - '0'));
    }
    return asm_sym_define(ctx, stmt->label, stmt->label_len, ctx->section,
                          ctx->offset[ctx->section]);
}

static AsmStatus process_insn(AsmCtx* ctx, const AsmStmt* stmt) {
    AsmStatus status;
    uint32_t size;
    if (ctx->section == ASM_SECTION_BSS)
        return asm_ctx_error(ctx, ASM_ERR_WRONG_SECTION);
    status = asm_insn_size(stmt, &size);
    if (status != ASM_OK)
        return asm_ctx_error(ctx, status);
    if (ctx->pass == 1)
        return advance(ctx, size);
    return asm_encode_insn(ctx, stmt);
}

static uint8_t data_unit(const AsmStmt* stmt) {
    if (stmt->size == ASM_SIZE_B)
        return 1;
    if (stmt->size == ASM_SIZE_W)
        return 2;
    return 4;
}

static AsmStatus process_data(AsmCtx* ctx, const AsmStmt* stmt) {
    uint8_t unit = data_unit(stmt);
    uint8_t type = unit == 1   ? ASM_RELOC_ABS8
                   : unit == 2 ? ASM_RELOC_ABS16
                               : ASM_RELOC_ABS32;
    uint8_t i;
    if (ctx->section == ASM_SECTION_BSS)
        return asm_ctx_error(ctx, ASM_ERR_WRONG_SECTION);
    if (ctx->pass == 1)
        return advance(ctx, (uint32_t)stmt->value_count * unit);
    for (i = 0; i < stmt->value_count; i++) {
        AsmValue value;
        uint32_t at = ctx->offset[ctx->section];
        AsmStatus status = asm_expr_eval(ctx, &stmt->values[i], &value);
        if (status != ASM_OK)
            return asm_ctx_error(ctx, status);
        if (value.kind == ASM_VAL_ABS) {
            int32_t v = (int32_t)value.value;
            if (unit == 1 && (v < -128 || v > 255))
                return asm_ctx_error(ctx, ASM_ERR_RANGE);
            if (unit == 2 && (v < -32768 || v > 65535))
                return asm_ctx_error(ctx, ASM_ERR_RANGE);
            if (unit == 1)
                status = asm_emit8(ctx, value.value);
            else if (unit == 2)
                status = asm_emit16(ctx, value.value);
            else
                status = asm_emit32(ctx, value.value);
            if (status != ASM_OK)
                return status;
        } else {
            int32_t addend = (int32_t)value.value;
            uint32_t placeholder = value.kind == ASM_VAL_REL ? value.value : 0;
            int symbol;
            if (value.kind == ASM_VAL_EXTERN) {
                symbol = (int)value.symbol;
            } else {
                symbol = asm_section_symbol(ctx, value.section);
                if (symbol < 0)
                    return asm_ctx_error(ctx, ASM_ERR_SYMBOL_TABLE_FULL);
            }
            if (unit == 1)
                status = asm_emit8(ctx, placeholder);
            else if (unit == 2)
                status = asm_emit16(ctx, placeholder);
            else
                status = asm_emit32(ctx, placeholder);
            if (status != ASM_OK)
                return status;
            status = asm_reloc_add(ctx, ctx->section, type, at,
                                   (uint32_t)symbol, addend);
            if (status != ASM_OK)
                return status;
        }
    }
    return ASM_OK;
}

static AsmStatus process_ascii(AsmCtx* ctx, const AsmStmt* stmt) {
    uint32_t length = stmt->string_len + (stmt->string_zero ? 1u : 0u);
    AsmStatus status;
    if (ctx->section == ASM_SECTION_BSS)
        return asm_ctx_error(ctx, ASM_ERR_WRONG_SECTION);
    if (ctx->pass == 1)
        return advance(ctx, length);
    status =
        asm_emit_bytes(ctx, (const uint8_t*)stmt->string, stmt->string_len);
    if (status != ASM_OK)
        return status;
    if (stmt->string_zero)
        return asm_emit8(ctx, 0);
    return ASM_OK;
}

static AsmStatus process_space(AsmCtx* ctx, const AsmStmt* stmt) {
    uint8_t unit = stmt->ops[0].base_reg ? stmt->ops[0].base_reg : 1;
    AsmValue value;
    uint32_t count;
    AsmStatus status = asm_expr_eval(ctx, &stmt->expr, &value);
    if (status != ASM_OK)
        return asm_ctx_error(ctx, status);
    if (value.kind != ASM_VAL_ABS)
        return asm_ctx_error(ctx, ASM_ERR_NOT_ABSOLUTE);
    if (value.value > ASM_BSS_MAX)
        return asm_ctx_error(ctx, ASM_ERR_RANGE);
    count = value.value * unit;
    if (ctx->pass == 1)
        return advance(ctx, count);
    if (ctx->section == ASM_SECTION_BSS)
        return advance(ctx, count);
    {
        /* Emit zero fill in bounded chunks; text/data only. */
        uint8_t zeros[16];
        uint8_t i;
        for (i = 0; i < sizeof(zeros); i++) zeros[i] = 0;
        while (count) {
            uint32_t n = count > sizeof(zeros) ? sizeof(zeros) : count;
            status = asm_emit_bytes(ctx, zeros, n);
            if (status != ASM_OK)
                return status;
            count -= n;
        }
    }
    return ASM_OK;
}

static AsmStatus process_equ(AsmCtx* ctx, const AsmStmt* stmt) {
    AsmValue value;
    AsmStatus status;
    if (ctx->pass != 1)
        return ASM_OK;
    status = asm_expr_eval(ctx, &stmt->expr, &value);
    if (status != ASM_OK)
        return asm_ctx_error(ctx, status);
    if (value.kind != ASM_VAL_ABS)
        return asm_ctx_error(ctx, ASM_ERR_NOT_ABSOLUTE);
    return asm_sym_define(ctx, stmt->name, stmt->name_len, ASM_SECTION_ABS,
                          value.value);
}

static AsmStatus process_global(AsmCtx* ctx, const AsmStmt* stmt) {
    int existing = asm_sym_lookup(ctx, stmt->name, stmt->name_len);
    uint8_t i;
    if (existing >= 0) {
        ctx->symbols[existing].flags |= ASM_SYM_GLOBAL;
        return ASM_OK;
    }
    if (ctx->global_count >= ASM_GLOBAL_MAX)
        return asm_ctx_error(ctx, ASM_ERR_SYMBOL_TABLE_FULL);
    for (i = 0; i < stmt->name_len; i++)
        ctx->globals[ctx->global_count].name[i] = stmt->name[i];
    ctx->globals[ctx->global_count].length = stmt->name_len;
    ctx->global_count++;
    return ASM_OK;
}

static AsmStatus process_even(AsmCtx* ctx) {
    if (ctx->pass == 1) {
        if (ctx->offset[ctx->section] & 1u)
            return advance(ctx, 1);
        return ASM_OK;
    }
    return asm_align_even(ctx);
}

static AsmStatus process_stmt(AsmCtx* ctx, const AsmStmt* stmt) {
    AsmStatus status = define_label(ctx, stmt);
    if (status != ASM_OK)
        return status;
    switch (stmt->kind) {
    case ASM_STMT_EMPTY: return ASM_OK;
    case ASM_STMT_END: ctx->ended = 1; return ASM_OK;
    case ASM_STMT_SECTION: ctx->section = stmt->section; return ASM_OK;
    case ASM_STMT_EVEN: return process_even(ctx);
    case ASM_STMT_INSN: return process_insn(ctx, stmt);
    case ASM_STMT_DATA: return process_data(ctx, stmt);
    case ASM_STMT_ASCII: return process_ascii(ctx, stmt);
    case ASM_STMT_SPACE: return process_space(ctx, stmt);
    case ASM_STMT_EQU: return process_equ(ctx, stmt);
    case ASM_STMT_GLOBAL: return process_global(ctx, stmt);
    case ASM_STMT_EXTERN:
        return asm_sym_extern(ctx, stmt->name, stmt->name_len);
    default: return asm_ctx_error(ctx, ASM_ERR_SYNTAX);
    }
}

void asm_assemble_begin(AsmCtx* ctx, uint32_t pass) {
    uint8_t i;
    ctx->pass = pass;
    ctx->line = 0;
    ctx->ended = 0;
    for (i = 0; i < 4; i++) ctx->offset[i] = 0;
}

AsmStatus asm_assemble_line(AsmCtx* ctx, const char* line) {
    AsmToken tokens[ASM_TOKEN_MAX];
    AsmStmt stmt;
    uint8_t count = 0;
    AsmStatus status;
    if (ctx->status != ASM_OK)
        return ctx->status;
    if (ctx->ended)
        return ASM_OK;
    ctx->line++;
    if (ctx->line > ASM_LINE_COUNT_MAX)
        return asm_ctx_error(ctx, ASM_ERR_SOURCE_LONG);
    status = asm_lex(line, tokens, ASM_TOKEN_MAX, &count);
    if (status != ASM_OK)
        return asm_ctx_error(ctx, status);
    status = asm_parse(tokens, count, &stmt);
    if (status != ASM_OK)
        return asm_ctx_error(ctx, status);
    return process_stmt(ctx, &stmt);
}

AsmStatus asm_assemble_end(AsmCtx* ctx) {
    uint32_t i;
    ctx->bss_size = ctx->offset[ASM_SECTION_BSS];
    if (ctx->pass != 2)
        return ctx->status;
    for (i = 0; i < ctx->global_count;) {
        int existing =
            asm_sym_lookup(ctx, ctx->globals[i].name, ctx->globals[i].length);
        if (existing < 0)
            return asm_ctx_error(ctx, ASM_ERR_UNDEFINED_SYMBOL);
        ctx->symbols[existing].flags |= ASM_SYM_GLOBAL;
        ctx->global_count--; /* compact: swap with the last entry */
        if (i < ctx->global_count) {
            uint8_t j;
            for (j = 0; j < ctx->globals[ctx->global_count].length; j++)
                ctx->globals[i].name[j] =
                    ctx->globals[ctx->global_count].name[j];
            ctx->globals[i].length = ctx->globals[ctx->global_count].length;
        }
    }
    ctx->global_count = 0;
    return ctx->status;
}

/* Derives the default output name: the source name with its extension
 * replaced by ".O" (contract CLI: run asm SRC [DST]). The source base
 * must fit the FAT16 8.3 rule; a longer source is rejected so the 16
 * byte output buffer can never overflow. */
AsmStatus asm_default_output(const char* source, char* out) {
    uint32_t base = 0;
    uint32_t full = 0;
    uint32_t i;
    while (source[full]) full++;
    base = full;
    while (base > 0 && source[base - 1u] != '.') base--;
    if (base > 0)
        base--; /* drop the '.' itself; a bare ".O" is not 8.3 */
    if (base == 0)
        base = full; /* no extension: keep the base name */
    if (base > 8)
        return ASM_ERR_NAME_LONG; /* callers report their own message */
    for (i = 0; i < base; i++) out[i] = source[i];
    out[i++] = '.';
    out[i++] = 'O';
    out[i] = 0;
    return ASM_OK;
}
