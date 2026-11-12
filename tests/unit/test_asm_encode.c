/* Host unit tests for apps/asm/encode.c.
 *
 * Builds AsmStmt values by hand and links local test-only stubs for the
 * parse.c/ctx.c pieces encode.c depends on (asm_expr_eval and
 * asm_section_symbol); lex.c/parse.c are not linked. Expected machine
 * bytes were verified against m68k-xelf-as -m68000 (binutils 2.46). */

#include "../../apps/asm/asm.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* --- test-only stubs ------------------------------------------------ */

/* Minimal expression evaluator: NUMBER, IDENT (fixed symbol table,
 * installed per test context by install_syms), '.', and IDENT+/-NUMBER. */
AsmStatus asm_expr_eval(const AsmCtx* ctx, const AsmExpr* expr, AsmValue* out) {
    const AsmToken* t = &expr->ops[0];
    out->kind = ASM_VAL_ABS;
    out->section = ASM_SECTION_ABS;
    out->value = 0;
    out->symbol = 0;
    if (expr->count == 0)
        return ASM_OK;
    if (t->kind == ASM_TOK_NUMBER) {
        out->value = t->number;
    } else if (t->kind == ASM_TOK_IDENT) {
        int idx;
        if (t->length == 1 && t->text[0] == '.') {
            out->kind = ASM_VAL_REL;
            out->section = ctx->section;
            out->value = ctx->offset[ctx->section];
        } else {
            const AsmSymbol* sym;
            idx = asm_sym_lookup(ctx, t->text, t->length);
            if (idx < 0)
                return ASM_ERR_UNDEFINED_SYMBOL;
            sym = &ctx->symbols[idx];
            if (sym->flags & ASM_SYM_EXTERN) {
                out->kind = ASM_VAL_EXTERN;
                out->symbol = (uint32_t)idx;
                out->value = 0;
            } else if (sym->section == ASM_SECTION_ABS) {
                out->value = sym->value;
            } else {
                out->kind = ASM_VAL_REL;
                out->section = sym->section;
                out->value = sym->value;
            }
        }
    } else {
        return ASM_ERR_SYNTAX;
    }
    if (expr->count == 3) {
        const AsmToken* op = &expr->ops[1];
        const AsmToken* rhs = &expr->ops[2];
        if (rhs->kind != ASM_TOK_NUMBER)
            return ASM_ERR_SYNTAX;
        if (op->kind == ASM_TOK_PLUS)
            out->value += rhs->number;
        else if (op->kind == ASM_TOK_MINUS)
            out->value -= rhs->number;
        else
            return ASM_ERR_SYNTAX;
    } else if (expr->count != 1) {
        return ASM_ERR_SYNTAX;
    }
    return ASM_OK;
}

/* asm_section_symbol() is provided by ctx.c (section anchor symbols with
 * names "\001text"/"\001data"/"\001bss", flags ASM_SYM_SECTION, value 0). */

/* --- statement builders --------------------------------------------- */

static AsmToken tk_num(uint32_t n) {
    AsmToken t;
    memset(&t, 0, sizeof(t));
    t.kind = ASM_TOK_NUMBER;
    t.number = n;
    return t;
}

static AsmToken tk_id(const char* s) {
    AsmToken t;
    memset(&t, 0, sizeof(t));
    t.kind = ASM_TOK_IDENT;
    t.text = s;
    t.length = (uint8_t)strlen(s);
    return t;
}

static AsmExpr ex_num(uint32_t v) {
    AsmExpr e;
    memset(&e, 0, sizeof(e));
    e.ops[0] = tk_num(v);
    e.count = 1;
    return e;
}

static AsmExpr ex_id(const char* s) {
    AsmExpr e;
    memset(&e, 0, sizeof(e));
    e.ops[0] = tk_id(s);
    e.count = 1;
    return e;
}

static AsmExpr ex_id_off(const char* s, int neg, uint32_t off) {
    AsmExpr e;
    memset(&e, 0, sizeof(e));
    e.ops[0] = tk_id(s);
    e.ops[1].kind = neg ? ASM_TOK_MINUS : ASM_TOK_PLUS;
    e.ops[2] = tk_num(off);
    e.count = 3;
    return e;
}

static AsmOperand op_none(void) {
    AsmOperand o;
    memset(&o, 0, sizeof(o));
    return o;
}

static AsmOperand op_dn(int n) {
    AsmOperand o = op_none();
    o.mode = ASM_OP_REG;
    o.base_reg = (uint8_t)(ASM_REG_D0 + n);
    return o;
}

static AsmOperand op_an(int n) {
    AsmOperand o = op_none();
    o.mode = ASM_OP_REG;
    o.base_reg = (uint8_t)(ASM_REG_A0 + n);
    return o;
}

static AsmOperand op_reg(uint8_t reg) {
    AsmOperand o = op_none();
    o.mode = ASM_OP_REG;
    o.base_reg = reg;
    return o;
}

static AsmOperand op_imm(uint32_t v) {
    AsmOperand o = op_none();
    o.mode = ASM_OP_IMM;
    o.expr = ex_num(v);
    return o;
}

static AsmOperand op_imm_expr(AsmExpr e) {
    AsmOperand o = op_none();
    o.mode = ASM_OP_IMM;
    o.expr = e;
    return o;
}

static AsmOperand op_abs(uint32_t v) {
    AsmOperand o = op_none();
    o.mode = ASM_OP_ABS;
    o.expr = ex_num(v);
    return o;
}

static AsmOperand op_abs_w(uint32_t v) {
    AsmOperand o = op_abs(v);
    o.abs_size = ASM_SIZE_W;
    return o;
}

static AsmOperand op_abs_expr(AsmExpr e) {
    AsmOperand o = op_none();
    o.mode = ASM_OP_ABS;
    o.expr = e;
    return o;
}

static AsmOperand op_abs_w_expr(AsmExpr e) {
    AsmOperand o = op_abs_expr(e);
    o.abs_size = ASM_SIZE_W;
    return o;
}

static AsmOperand op_mem(int an) { /* bare (%an) */
    AsmOperand o = op_none();
    o.mode = ASM_OP_MEM;
    o.base_reg = (uint8_t)(ASM_REG_A0 + an);
    o.expr.count = 0;
    return o;
}

static AsmOperand op_d16an(int an, uint32_t d) {
    AsmOperand o = op_none();
    o.mode = ASM_OP_MEM;
    o.base_reg = (uint8_t)(ASM_REG_A0 + an);
    o.expr = ex_num(d);
    return o;
}

static AsmOperand op_mem_expr(int an, AsmExpr e) {
    AsmOperand o = op_none();
    o.mode = ASM_OP_MEM;
    o.base_reg = (uint8_t)(ASM_REG_A0 + an);
    o.expr = e;
    return o;
}

static AsmOperand op_pc_expr(AsmExpr e) {
    AsmOperand o = op_none();
    o.mode = ASM_OP_MEM;
    o.base_reg = ASM_REG_PC;
    o.expr = e;
    return o;
}

static AsmOperand op_pc(uint32_t d) { return op_pc_expr(ex_num(d)); }

static AsmOperand op_ix(int base_is_pc, int an, int idx, int lng, uint32_t d) {
    AsmOperand o = op_none();
    o.mode = ASM_OP_INDEX;
    o.base_reg = base_is_pc ? ASM_REG_PC : (uint8_t)(ASM_REG_A0 + an);
    o.index_reg = (uint8_t)(idx < 8 ? ASM_REG_D0 + idx : ASM_REG_A0 + idx - 8);
    o.index_long = (uint8_t)lng;
    o.expr = ex_num(d);
    return o;
}

static AsmOperand op_ix_expr(int base_is_pc, int an, int idx, int lng,
                             AsmExpr e) {
    AsmOperand o = op_none();
    o.mode = ASM_OP_INDEX;
    o.base_reg = base_is_pc ? ASM_REG_PC : (uint8_t)(ASM_REG_A0 + an);
    o.index_reg = (uint8_t)(idx < 8 ? ASM_REG_D0 + idx : ASM_REG_A0 + idx - 8);
    o.index_long = (uint8_t)lng;
    o.expr = e;
    return o;
}

static AsmOperand op_post(int an) {
    AsmOperand o = op_none();
    o.mode = ASM_OP_MEM_POSTINC;
    o.base_reg = (uint8_t)(ASM_REG_A0 + an);
    return o;
}

static AsmOperand op_pre(int an) {
    AsmOperand o = op_none();
    o.mode = ASM_OP_MEM_PREDEC;
    o.base_reg = (uint8_t)(ASM_REG_A0 + an);
    return o;
}

static AsmOperand op_list(uint16_t mask) {
    AsmOperand o = op_none();
    o.mode = ASM_OP_REGLIST;
    o.reg_mask = mask;
    return o;
}

static AsmStmt insn(uint8_t mn, uint8_t size) {
    AsmStmt s;
    memset(&s, 0, sizeof(s));
    s.kind = ASM_STMT_INSN;
    s.mnemonic = mn;
    s.size = size;
    return s;
}

static AsmStmt insn_cc(uint8_t mn, uint8_t size, uint8_t cc) {
    AsmStmt s = insn(mn, size);
    s.cc = cc;
    return s;
}

static void ops(AsmStmt* s, AsmOperand a) {
    s->ops[0] = a;
    s->op_count = 1;
}

static void ops2(AsmStmt* s, AsmOperand a, AsmOperand b) {
    s->ops[0] = a;
    s->ops[1] = b;
    s->op_count = 2;
}

/* --- harness --------------------------------------------------------- */

static int checks;
static int fails;

static void install_syms(AsmCtx* ctx) {
    asm_sym_define(ctx, "tlbl", 4, ASM_SECTION_TEXT, 0x0100);
    asm_sym_define(ctx, "tnext", 5, ASM_SECTION_TEXT, 0x0040);
    asm_sym_define(ctx, "tback", 5, ASM_SECTION_TEXT, 0x0010);
    asm_sym_define(ctx, "dlbl", 4, ASM_SECTION_DATA, 0x0020);
    asm_sym_define(ctx, "absym", 5, ASM_SECTION_ABS, 0x1234);
    asm_sym_extern(ctx, "xtrn", 4);
}

static AsmCtx base_ctx(uint32_t text_at) {
    AsmCtx ctx;
    asm_ctx_init(&ctx);
    ctx.pass = 2;
    ctx.line = 7;
    install_syms(&ctx);
    ctx.offset[ASM_SECTION_TEXT] = text_at;
    return ctx;
}

static void fail(const char* name, const char* fmt, ...) {
    va_list ap;
    fails++;
    printf("FAIL %s: ", name);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static void ok(const char* name, const AsmStmt* s, const uint8_t* want,
               uint32_t n) {
    AsmCtx ctx = base_ctx(0);
    AsmStatus st = asm_encode_insn(&ctx, s);
    uint32_t i;
    uint32_t size = 0xFFFFFFFFu;
    checks++;
    printf("%s\n", name);
    if (st != ASM_OK) {
        fail(name, "encode status %s", asm_status_message(st));
        return;
    }
    if (ctx.offset[ASM_SECTION_TEXT] != n) {
        fail(name, "emitted %u bytes, want %u", ctx.offset[ASM_SECTION_TEXT],
             n);
        return;
    }
    for (i = 0; i < n; i++) {
        if (ctx.text[i] != want[i]) {
            fail(name, "byte %u: %02X want %02X", i, ctx.text[i], want[i]);
            return;
        }
    }
    st = asm_insn_size(s, &size);
    if (st != ASM_OK || size != n) {
        fail(name, "size pass: status %s size %u want %u",
             asm_status_message(st), size, n);
    }
}

/* Success case plus exactly one expected relocation record. */
static void ok_reloc(const char* name, const AsmStmt* s, const uint8_t* want,
                     uint32_t n, uint8_t type, uint32_t roff, const char* rsym,
                     int32_t addend) {
    AsmCtx ctx = base_ctx(0);
    AsmStatus st = asm_encode_insn(&ctx, s);
    const AsmReloc* r;
    uint32_t i;
    int sym;
    checks++;
    printf("%s\n", name);
    if (st != ASM_OK) {
        fail(name, "encode status %s", asm_status_message(st));
        return;
    }
    for (i = 0; i < n; i++) {
        if (ctx.text[i] != want[i]) {
            fail(name, "byte %u: %02X want %02X", i, ctx.text[i], want[i]);
            return;
        }
    }
    if (ctx.reloc_count != 1) {
        fail(name, "reloc count %u, want 1", ctx.reloc_count);
        return;
    }
    r = &ctx.relocs[0];
    sym = asm_sym_lookup(&ctx, rsym, (uint8_t)strlen(rsym));
    if (r->type != type || r->offset != roff ||
        r->section != ASM_SECTION_TEXT || (int)r->symbol != sym ||
        r->addend != addend) {
        fail(name,
             "reloc type %u off %u sec %u sym %d add %d "
             "(want %u %u %u %d %d)",
             r->type, r->offset, r->section, (int)r->symbol, r->addend, type,
             roff, ASM_SECTION_TEXT, sym, addend);
    }
}

static void er(const char* name, const AsmStmt* s, AsmStatus code,
               AsmStatus size_code) {
    AsmCtx ctx = base_ctx(0);
    AsmStatus st = asm_encode_insn(&ctx, s);
    uint32_t size = 0;
    checks++;
    printf("%s\n", name);
    if (st != code) {
        fail(name, "status %s, want %s", asm_status_message(st),
             asm_status_message(code));
        return;
    }
    if (ctx.reloc_count != 0 || ctx.offset[ASM_SECTION_TEXT] != 0) {
        fail(name, "emitted bytes on error");
        return;
    }
    st = asm_insn_size(s, &size);
    if (st != size_code) {
        fail(name, "size status %s, want %s", asm_status_message(st),
             asm_status_message(size_code));
    }
}

#define B(...)                                                                 \
    (const uint8_t[]) { __VA_ARGS__ }

int main(void) {
    AsmStmt s;

    /* --- move: every addressing mode (gas b1) --- */
    s = insn(ASM_MN_MOVE, ASM_SIZE_B);
    ops2(&s, op_dn(1), op_dn(2));
    ok("move.b %d1,%d2", &s, B(0x14, 0x01), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_dn(1), op_dn(2));
    ok("move.w %d1,%d2", &s, B(0x34, 0x01), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_L);
    ops2(&s, op_dn(1), op_dn(2));
    ok("move.l %d1,%d2", &s, B(0x24, 0x01), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_an(1), op_dn(2));
    ok("move.w %a1,%d2", &s, B(0x34, 0x09), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_L);
    ops2(&s, op_an(1), op_an(2));
    ok("move.l %a1,%a2", &s, B(0x24, 0x49), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_mem(1), op_dn(2));
    ok("move.w (%a1),%d2", &s, B(0x34, 0x11), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_post(1), op_dn(2));
    ok("move.w (%a1)+,%d2", &s, B(0x34, 0x19), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_B);
    ops2(&s, op_pre(1), op_dn(2));
    ok("move.b -(%a1),%d2", &s, B(0x14, 0x21), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_d16an(1, 8), op_dn(2));
    ok("move.w 8(%a1),%d2", &s, B(0x34, 0x29, 0x00, 0x08), 4);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_ix(0, 1, 3, 1, 8), op_dn(2));
    ok("move.w 8(%a1,%d3.l),%d2", &s, B(0x34, 0x31, 0x38, 0x08), 4);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_ix(0, 1, 3, 0, 8), op_dn(2));
    ok("move.w 8(%a1,%d3.w),%d2", &s, B(0x34, 0x31, 0x30, 0x08), 4);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_ix(0, 1, 11, 1, 8), op_dn(2));
    ok("move.w 8(%a1,%a3.l),%d2", &s, B(0x34, 0x31, 0xB8, 0x08), 4);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_pc(4), op_dn(2));
    ok("move.w 4(%pc),%d2", &s, B(0x34, 0x3A, 0x00, 0x04), 4);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_abs_w(0x1234), op_dn(2));
    ok("move.w 0x1234.w,%d2", &s, B(0x34, 0x38, 0x12, 0x34), 4);
    s = insn(ASM_MN_MOVE, ASM_SIZE_L);
    ops2(&s, op_abs(0x12345678u), op_dn(2));
    ok("move.l 0x12345678,%d2", &s, B(0x24, 0x39, 0x12, 0x34, 0x56, 0x78), 6);
    /* absolute .w/.l suffix vectors, verified against m68k-xelf-as
     * -m68000: move.l 4.w encodes 2038 0004, move.l 4.l 2039 00000004 */
    s = insn(ASM_MN_MOVE, ASM_SIZE_L);
    ops2(&s, op_abs_w(4), op_dn(0));
    ok("move.l 4.w,%d0", &s, B(0x20, 0x38, 0x00, 0x04), 4);
    s = insn(ASM_MN_MOVE, ASM_SIZE_L);
    ops2(&s, op_abs(4), op_dn(0));
    ok("move.l 4.l,%d0", &s, B(0x20, 0x39, 0x00, 0x00, 0x00, 0x04), 6);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_imm(0x1234), op_dn(2));
    ok("move.w #0x1234,%d2", &s, B(0x34, 0x3C, 0x12, 0x34), 4);
    s = insn(ASM_MN_MOVE, ASM_SIZE_B);
    ops2(&s, op_imm(0x12), op_dn(2));
    ok("move.b #0x12,%d2", &s, B(0x14, 0x3C, 0x00, 0x12), 4);
    s = insn(ASM_MN_MOVE, ASM_SIZE_L);
    ops2(&s, op_imm(0x12345678u), op_dn(2));
    ok("move.l #0x12345678,%d2", &s, B(0x24, 0x3C, 0x12, 0x34, 0x56, 0x78), 6);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_dn(2), op_d16an(1, 8));
    ok("move.w %d2,8(%a1)", &s, B(0x33, 0x42, 0x00, 0x08), 4);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_dn(2), op_post(1));
    ok("move.w %d2,(%a1)+", &s, B(0x32, 0xC2), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_dn(2), op_pre(1));
    ok("move.w %d2,-(%a1)", &s, B(0x33, 0x02), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_dn(2), op_abs_w(0x1234));
    ok("move.w %d2,0x1234.w", &s, B(0x31, 0xC2, 0x12, 0x34), 4);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_dn(2), op_abs(0x12345678u));
    ok("move.w %d2,0x12345678", &s, B(0x33, 0xC2, 0x12, 0x34, 0x56, 0x78), 6);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_dn(2), op_an(2));
    ok("move.w %d2,%a2 (movea form)", &s, B(0x34, 0x42), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_d16an(1, 8), op_an(2));
    ok("move.w 8(%a1),%a2 (movea form)", &s, B(0x34, 0x69, 0x00, 0x08), 4);

    /* --- movea/moveq/special registers --- */
    s = insn(ASM_MN_MOVEA, ASM_SIZE_L);
    ops2(&s, op_abs(0x12345678u), op_an(2));
    ok("movea.l 0x12345678,%a2", &s, B(0x24, 0x79, 0x12, 0x34, 0x56, 0x78), 6);
    s = insn(ASM_MN_MOVEQ, ASM_SIZE_NONE);
    ops2(&s, op_imm(0xFFFFFF80u), op_dn(0));
    ok("moveq #-128,%d0", &s, B(0x70, 0x80), 2);
    s = insn(ASM_MN_MOVEQ, ASM_SIZE_NONE);
    ops2(&s, op_imm(127), op_dn(7));
    ok("moveq #127,%d7", &s, B(0x7E, 0x7F), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_dn(0), op_reg(ASM_REG_CCR));
    ok("move.w %d0,%ccr", &s, B(0x44, 0xC0), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_NONE);
    ops2(&s, op_imm(4), op_reg(ASM_REG_CCR));
    ok("move #4,%ccr", &s, B(0x44, 0xFC, 0x00, 0x04), 4);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_reg(ASM_REG_SR), op_dn(0));
    ok("move.w %sr,%d0", &s, B(0x40, 0xC0), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_dn(0), op_reg(ASM_REG_SR));
    ok("move.w %d0,%sr", &s, B(0x46, 0xC0), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_L);
    ops2(&s, op_an(0), op_reg(ASM_REG_USP));
    ok("move.l %a0,%usp", &s, B(0x4E, 0x60), 2);
    s = insn(ASM_MN_MOVE, ASM_SIZE_L);
    ops2(&s, op_reg(ASM_REG_USP), op_an(3));
    ok("move.l %usp,%a3", &s, B(0x4E, 0x6B), 2);

    /* move errors */
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_reg(ASM_REG_CCR), op_dn(0));
    er("move.w %ccr,%d0 rejected (68010+)", &s, ASM_ERR_UNSUPPORTED,
       ASM_ERR_UNSUPPORTED);
    s = insn(ASM_MN_MOVE, ASM_SIZE_B);
    ops2(&s, op_an(0), op_dn(1));
    er("move.b %a0,%d1 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_MOVE, ASM_SIZE_NONE);
    ops2(&s, op_dn(0), op_dn(1));
    er("move without size rejected", &s, ASM_ERR_BAD_SIZE, ASM_ERR_BAD_SIZE);
    s = insn(ASM_MN_MOVEQ, ASM_SIZE_NONE);
    ops2(&s, op_imm(128), op_dn(0));
    er("moveq #128 out of range", &s, ASM_ERR_RANGE, ASM_OK);
    s = insn(ASM_MN_MOVEQ, ASM_SIZE_NONE);
    ops2(&s, op_dn(0), op_dn(1));
    er("moveq %d0,%d1 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_MOVEQ, ASM_SIZE_L);
    ops2(&s, op_imm(1), op_dn(0));
    er("moveq.l rejected", &s, ASM_ERR_BAD_SIZE, ASM_ERR_BAD_SIZE);

    /* --- movem --- */
    s = insn(ASM_MN_MOVEM, ASM_SIZE_L);
    ops2(&s, op_list(0x010F), op_pre(1));
    ok("movem.l %d0-%d3/%a0,-(%a1)", &s, B(0x48, 0xE1, 0xF0, 0x80), 4);
    s = insn(ASM_MN_MOVEM, ASM_SIZE_W);
    ops2(&s, op_list(0x010F), op_pre(1));
    ok("movem.w %d0-%d3/%a0,-(%a1)", &s, B(0x48, 0xA1, 0xF0, 0x80), 4);
    s = insn(ASM_MN_MOVEM, ASM_SIZE_L);
    ops2(&s, op_list(0xFFFF), op_pre(7));
    ok("movem.l all,-(%sp)", &s, B(0x48, 0xE7, 0xFF, 0xFF), 4);
    s = insn(ASM_MN_MOVEM, ASM_SIZE_L);
    ops2(&s, op_post(1), op_list(0x010F));
    ok("movem.l (%a1)+,%d0-%d3/%a0", &s, B(0x4C, 0xD9, 0x01, 0x0F), 4);
    s = insn(ASM_MN_MOVEM, ASM_SIZE_W);
    ops2(&s, op_post(1), op_list(0x0007));
    ok("movem.w (%a1)+,%d0-%d2", &s, B(0x4C, 0x99, 0x00, 0x07), 4);
    s = insn(ASM_MN_MOVEM, ASM_SIZE_L);
    ops2(&s, op_d16an(1, 4), op_list(0x0027));
    ok("movem.l 4(%a1),%d0-%d2/%d5", &s, B(0x4C, 0xE9, 0x00, 0x27, 0x00, 0x04),
       6);
    s = insn(ASM_MN_MOVEM, ASM_SIZE_L);
    ops2(&s, op_list(0x0003), op_d16an(1, 4));
    ok("movem.l %d0-%d1,4(%a1)", &s, B(0x48, 0xE9, 0x00, 0x03, 0x00, 0x04), 6);
    s = insn(ASM_MN_MOVEM, ASM_SIZE_W);
    ops2(&s, op_abs_w(0x1234), op_list(0x0002));
    ok("movem.w 0x1234,%d1", &s, B(0x4C, 0xB8, 0x00, 0x02, 0x12, 0x34), 6);
    s = insn(ASM_MN_MOVEM, ASM_SIZE_L);
    ops2(&s, op_list(0x0001), op_mem(1));
    ok("movem.l %d0,(%a1)", &s, B(0x48, 0xD1, 0x00, 0x01), 4);
    s = insn(ASM_MN_MOVEM, ASM_SIZE_L);
    ops2(&s, op_pc_expr(ex_id("tlbl")), op_list(0x0003));
    ok("movem.l tlbl(%pc),%d0-%d1", &s, B(0x4C, 0xFA, 0x00, 0x03, 0x00, 0xFC),
       6);
    s = insn(ASM_MN_MOVEM, ASM_SIZE_B);
    ops2(&s, op_list(1), op_mem(1));
    er("movem.b rejected", &s, ASM_ERR_BAD_SIZE, ASM_ERR_BAD_SIZE);
    s = insn(ASM_MN_MOVEM, ASM_SIZE_L);
    ops2(&s, op_list(1), op_post(1));
    er("movem.l regs,(%a1)+ rejected", &s, ASM_ERR_BAD_OPERAND,
       ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_MOVEM, ASM_SIZE_L);
    ops2(&s, op_pre(1), op_list(1));
    er("movem.l -(%a1),regs rejected", &s, ASM_ERR_BAD_OPERAND,
       ASM_ERR_BAD_OPERAND);

    /* --- movep --- */
    s = insn(ASM_MN_MOVEP, ASM_SIZE_W);
    ops2(&s, op_dn(0), op_d16an(1, 8));
    ok("movep.w %d0,8(%a1)", &s, B(0x01, 0x89, 0x00, 0x08), 4);
    s = insn(ASM_MN_MOVEP, ASM_SIZE_L);
    ops2(&s, op_dn(0), op_d16an(1, 8));
    ok("movep.l %d0,8(%a1)", &s, B(0x01, 0xC9, 0x00, 0x08), 4);
    s = insn(ASM_MN_MOVEP, ASM_SIZE_W);
    ops2(&s, op_d16an(1, 8), op_dn(0));
    ok("movep.w 8(%a1),%d0", &s, B(0x01, 0x09, 0x00, 0x08), 4);
    s = insn(ASM_MN_MOVEP, ASM_SIZE_L);
    ops2(&s, op_d16an(1, 8), op_dn(0));
    ok("movep.l 8(%a1),%d0", &s, B(0x01, 0x49, 0x00, 0x08), 4);
    s = insn(ASM_MN_MOVEP, ASM_SIZE_W);
    ops2(&s, op_dn(0), op_mem(1));
    er("movep.w %d0,(%a1) needs d16", &s, ASM_ERR_BAD_OPERAND,
       ASM_ERR_BAD_OPERAND);

    /* --- lea/pea --- */
    s = insn(ASM_MN_LEA, ASM_SIZE_NONE);
    ops2(&s, op_d16an(1, 8), op_an(2));
    ok("lea 8(%a1),%a2", &s, B(0x45, 0xE9, 0x00, 0x08), 4);
    s = insn(ASM_MN_LEA, ASM_SIZE_NONE);
    ops2(&s, op_ix(0, 1, 3, 1, 8), op_an(2));
    ok("lea 8(%a1,%d3.l),%a2", &s, B(0x45, 0xF1, 0x38, 0x08), 4);
    s = insn(ASM_MN_LEA, ASM_SIZE_NONE);
    ops2(&s, op_abs(0x12345678u), op_an(2));
    ok("lea 0x12345678,%a2", &s, B(0x45, 0xF9, 0x12, 0x34, 0x56, 0x78), 6);
    s = insn(ASM_MN_LEA, ASM_SIZE_NONE);
    ops2(&s, op_abs_w(0x1234), op_an(2));
    ok("lea 0x1234.w,%a2", &s, B(0x45, 0xF8, 0x12, 0x34), 4);
    s = insn(ASM_MN_PEA, ASM_SIZE_NONE);
    ops(&s, op_d16an(1, 8));
    ok("pea 8(%a1)", &s, B(0x48, 0x69, 0x00, 0x08), 4);
    s = insn(ASM_MN_PEA, ASM_SIZE_NONE);
    ops(&s, op_abs(0x12345678u));
    ok("pea 0x12345678", &s, B(0x48, 0x79, 0x12, 0x34, 0x56, 0x78), 6);
    s = insn(ASM_MN_LEA, ASM_SIZE_NONE);
    ops2(&s, op_post(1), op_an(2));
    er("lea (%a1)+,%a2 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_LEA, ASM_SIZE_NONE);
    ops2(&s, op_dn(0), op_an(1));
    er("lea %d0,%a1 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_PEA, ASM_SIZE_NONE);
    ops(&s, op_post(1));
    er("pea (%a1)+ rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_LEA, ASM_SIZE_L);
    ops2(&s, op_mem(1), op_an(2));
    er("lea.l rejected", &s, ASM_ERR_BAD_SIZE, ASM_ERR_BAD_SIZE);

    /* --- clr family / exg / swap / ext / link / unlink --- */
    s = insn(ASM_MN_CLR, ASM_SIZE_B);
    ops(&s, op_dn(0));
    ok("clr.b %d0", &s, B(0x42, 0x00), 2);
    s = insn(ASM_MN_CLR, ASM_SIZE_W);
    ops(&s, op_mem(1));
    ok("clr.w (%a1)", &s, B(0x42, 0x51), 2);
    s = insn(ASM_MN_CLR, ASM_SIZE_L);
    ops(&s, op_ix(0, 1, 2, 1, 8));
    ok("clr.l 8(%a1,%d2.l)", &s, B(0x42, 0xB1, 0x28, 0x08), 4);
    s = insn(ASM_MN_NEG, ASM_SIZE_B);
    ops(&s, op_dn(0));
    ok("neg.b %d0", &s, B(0x44, 0x00), 2);
    s = insn(ASM_MN_NEGX, ASM_SIZE_W);
    ops(&s, op_mem(1));
    ok("negx.w (%a1)", &s, B(0x40, 0x51), 2);
    s = insn(ASM_MN_NOT, ASM_SIZE_L);
    ops(&s, op_d16an(1, 8));
    ok("not.l 8(%a1)", &s, B(0x46, 0xA9, 0x00, 0x08), 4);
    s = insn(ASM_MN_TST, ASM_SIZE_B);
    ops(&s, op_dn(0));
    ok("tst.b %d0", &s, B(0x4A, 0x00), 2);
    s = insn(ASM_MN_TST, ASM_SIZE_W);
    ops(&s, op_mem(1));
    ok("tst.w (%a1)", &s, B(0x4A, 0x51), 2);
    s = insn(ASM_MN_TST, ASM_SIZE_L);
    ops(&s, op_ix(0, 1, 2, 1, 8));
    ok("tst.l 8(%a1,%d2.l)", &s, B(0x4A, 0xB1, 0x28, 0x08), 4);
    s = insn(ASM_MN_NBCD, ASM_SIZE_NONE);
    ops(&s, op_dn(0));
    ok("nbcd %d0", &s, B(0x48, 0x00), 2);
    s = insn(ASM_MN_NBCD, ASM_SIZE_B);
    ops(&s, op_dn(0));
    ok("nbcd.b %d0 (bare form is .b)", &s, B(0x48, 0x00), 2);
    s = insn(ASM_MN_NBCD, ASM_SIZE_NONE);
    ops(&s, op_mem(1));
    ok("nbcd (%a1)", &s, B(0x48, 0x11), 2);
    s = insn(ASM_MN_TAS, ASM_SIZE_NONE);
    ops(&s, op_dn(0));
    ok("tas %d0", &s, B(0x4A, 0xC0), 2);
    s = insn(ASM_MN_TAS, ASM_SIZE_NONE);
    ops(&s, op_d16an(1, 8));
    ok("tas 8(%a1)", &s, B(0x4A, 0xE9, 0x00, 0x08), 4);
    s = insn(ASM_MN_EXG, ASM_SIZE_NONE);
    ops2(&s, op_dn(0), op_dn(1));
    ok("exg %d0,%d1", &s, B(0xC1, 0x41), 2);
    s = insn(ASM_MN_EXG, ASM_SIZE_NONE);
    ops2(&s, op_an(0), op_an(1));
    ok("exg %a0,%a1", &s, B(0xC1, 0x49), 2);
    s = insn(ASM_MN_EXG, ASM_SIZE_NONE);
    ops2(&s, op_dn(0), op_an(1));
    ok("exg %d0,%a1", &s, B(0xC1, 0x89), 2);
    s = insn(ASM_MN_EXG, ASM_SIZE_NONE);
    ops2(&s, op_an(0), op_dn(1));
    er("exg %a0,%d1 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_SWAP, ASM_SIZE_NONE);
    ops(&s, op_dn(0));
    ok("swap %d0", &s, B(0x48, 0x40), 2);
    s = insn(ASM_MN_EXT, ASM_SIZE_W);
    ops(&s, op_dn(0));
    ok("ext.w %d0", &s, B(0x48, 0x80), 2);
    s = insn(ASM_MN_EXT, ASM_SIZE_L);
    ops(&s, op_dn(0));
    ok("ext.l %d0", &s, B(0x48, 0xC0), 2);
    s = insn(ASM_MN_EXT, ASM_SIZE_B);
    ops(&s, op_dn(0));
    er("ext.b rejected", &s, ASM_ERR_BAD_SIZE, ASM_ERR_BAD_SIZE);
    s = insn(ASM_MN_LINK, ASM_SIZE_NONE);
    ops2(&s, op_an(0), op_imm(0xFFF8u));
    ok("link %a0,#-8", &s, B(0x4E, 0x50, 0xFF, 0xF8), 4);
    s = insn(ASM_MN_LINK, ASM_SIZE_NONE);
    ops2(&s, op_an(6), op_imm(16));
    ok("link %a6,#16", &s, B(0x4E, 0x56, 0x00, 0x10), 4);
    s = insn(ASM_MN_UNLINK, ASM_SIZE_NONE);
    ops(&s, op_an(7));
    ok("unlink %sp", &s, B(0x4E, 0x5F), 2);
    s = insn(ASM_MN_CLR, ASM_SIZE_W);
    ops(&s, op_an(0));
    er("clr.w %a0 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_TST, ASM_SIZE_W);
    ops(&s, op_imm(5));
    er("tst.w #5 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);

    /* --- arithmetic --- */
    s = insn(ASM_MN_ADD, ASM_SIZE_W);
    ops2(&s, op_dn(1), op_dn(2));
    ok("add.w %d1,%d2", &s, B(0xD4, 0x41), 2);
    s = insn(ASM_MN_ADD, ASM_SIZE_L);
    ops2(&s, op_mem(1), op_dn(2));
    ok("add.l (%a1),%d2", &s, B(0xD4, 0x91), 2);
    s = insn(ASM_MN_ADD, ASM_SIZE_W);
    ops2(&s, op_dn(2), op_d16an(1, 8));
    ok("add.w %d2,8(%a1)", &s, B(0xD5, 0x69, 0x00, 0x08), 4);
    s = insn(ASM_MN_ADD, ASM_SIZE_W);
    ops2(&s, op_dn(2), op_mem(1));
    ok("add.w %d2,(%a1)", &s, B(0xD5, 0x51), 2);
    s = insn(ASM_MN_ADDI, ASM_SIZE_W);
    ops2(&s, op_imm(0x1234), op_dn(2));
    ok("addi.w #0x1234,%d2", &s, B(0x06, 0x42, 0x12, 0x34), 4);
    s = insn(ASM_MN_ADDI, ASM_SIZE_B);
    ops2(&s, op_imm(0x12), op_mem(1));
    ok("addi.b #0x12,(%a1)", &s, B(0x06, 0x11, 0x00, 0x12), 4);
    s = insn(ASM_MN_ADDI, ASM_SIZE_L);
    ops2(&s, op_imm(0x12345678u), op_dn(2));
    ok("addi.l #0x12345678,%d2", &s, B(0x06, 0x82, 0x12, 0x34, 0x56, 0x78), 6);
    s = insn(ASM_MN_ADDQ, ASM_SIZE_B);
    ops2(&s, op_imm(1), op_dn(0));
    ok("addq.b #1,%d0", &s, B(0x52, 0x00), 2);
    s = insn(ASM_MN_ADDQ, ASM_SIZE_W);
    ops2(&s, op_imm(8), op_an(2));
    ok("addq.w #8,%a2", &s, B(0x50, 0x4A), 2);
    s = insn(ASM_MN_ADDQ, ASM_SIZE_L);
    ops2(&s, op_imm(4), op_mem(1));
    ok("addq.l #4,(%a1)", &s, B(0x58, 0x91), 2);
    s = insn(ASM_MN_ADDQ, ASM_SIZE_B);
    ops2(&s, op_imm(9), op_dn(0));
    er("addq.b #9 out of range", &s, ASM_ERR_RANGE, ASM_OK);
    s = insn(ASM_MN_ADDQ, ASM_SIZE_B);
    ops2(&s, op_imm(1), op_an(2));
    er("addq.b #1,%a2 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_ADD, ASM_SIZE_B);
    ops2(&s, op_imm(5), op_dn(0));
    er("add.b #5,%d0 rejected (use addi/addq)", &s, ASM_ERR_UNSUPPORTED,
       ASM_ERR_UNSUPPORTED);
    s = insn(ASM_MN_SUB, ASM_SIZE_W);
    ops2(&s, op_dn(1), op_dn(2));
    ok("sub.w %d1,%d2", &s, B(0x94, 0x41), 2);
    s = insn(ASM_MN_SUB, ASM_SIZE_W);
    ops2(&s, op_dn(2), op_d16an(1, 8));
    ok("sub.w %d2,8(%a1)", &s, B(0x95, 0x69, 0x00, 0x08), 4);
    s = insn(ASM_MN_SUBI, ASM_SIZE_W);
    ops2(&s, op_imm(0x1234), op_dn(2));
    ok("subi.w #0x1234,%d2", &s, B(0x04, 0x42, 0x12, 0x34), 4);
    s = insn(ASM_MN_SUBQ, ASM_SIZE_B);
    ops2(&s, op_imm(3), op_dn(0));
    ok("subq.b #3,%d0", &s, B(0x57, 0x00), 2);
    s = insn(ASM_MN_SUBQ, ASM_SIZE_L);
    ops2(&s, op_imm(8), op_an(2));
    ok("subq.l #8,%a2", &s, B(0x51, 0x8A), 2);
    s = insn(ASM_MN_ADDA, ASM_SIZE_W);
    ops2(&s, op_d16an(1, 8), op_an(2));
    ok("adda.w 8(%a1),%a2", &s, B(0xD4, 0xE9, 0x00, 0x08), 4);
    s = insn(ASM_MN_ADDA, ASM_SIZE_L);
    ops2(&s, op_imm(4), op_an(3));
    ok("adda.l #4,%a3", &s, B(0xD7, 0xFC, 0x00, 0x00, 0x00, 0x04), 6);
    s = insn(ASM_MN_SUBA, ASM_SIZE_W);
    ops2(&s, op_d16an(1, 8), op_an(2));
    ok("suba.w 8(%a1),%a2", &s, B(0x94, 0xE9, 0x00, 0x08), 4);
    s = insn(ASM_MN_ADD, ASM_SIZE_W);
    ops2(&s, op_d16an(1, 8), op_an(2));
    ok("add.w 8(%a1),%a2 -> adda.w", &s, B(0xD4, 0xE9, 0x00, 0x08), 4);
    s = insn(ASM_MN_ADDA, ASM_SIZE_W);
    ops2(&s, op_an(0), op_an(1));
    ok("adda.w %a0,%a1", &s, B(0xD2, 0xC8), 2);

    /* --- cmp family --- */
    s = insn(ASM_MN_CMP, ASM_SIZE_W);
    ops2(&s, op_dn(1), op_dn(2));
    ok("cmp.w %d1,%d2", &s, B(0xB4, 0x41), 2);
    s = insn(ASM_MN_CMP, ASM_SIZE_L);
    ops2(&s, op_mem(1), op_dn(2));
    ok("cmp.l (%a1),%d2", &s, B(0xB4, 0x91), 2);
    s = insn(ASM_MN_CMP, ASM_SIZE_W);
    ops2(&s, op_d16an(1, 8), op_an(2));
    ok("cmp.w 8(%a1),%a2 -> cmpa.w", &s, B(0xB4, 0xE9, 0x00, 0x08), 4);
    s = insn(ASM_MN_CMPI, ASM_SIZE_W);
    ops2(&s, op_imm(0x1234), op_dn(2));
    ok("cmpi.w #0x1234,%d2", &s, B(0x0C, 0x42, 0x12, 0x34), 4);
    s = insn(ASM_MN_CMPI, ASM_SIZE_B);
    ops2(&s, op_imm(1), op_mem(1));
    ok("cmpi.b #1,(%a1)", &s, B(0x0C, 0x11, 0x00, 0x01), 4);
    s = insn(ASM_MN_CMPI, ASM_SIZE_B);
    ops2(&s, op_imm(200), op_dn(0));
    ok("cmpi.b #200,%d0", &s, B(0x0C, 0x00, 0x00, 0xC8), 4);
    s = insn(ASM_MN_CMPI, ASM_SIZE_B);
    ops2(&s, op_imm(0xFFFFFF9Cu), op_dn(0));
    ok("cmpi.b #-100,%d0", &s, B(0x0C, 0x00, 0xFF, 0x9C), 4);
    s = insn(ASM_MN_CMPA, ASM_SIZE_L);
    ops2(&s, op_d16an(1, 8), op_an(2));
    ok("cmpa.l 8(%a1),%a2", &s, B(0xB5, 0xE9, 0x00, 0x08), 4);
    s = insn(ASM_MN_CMPM, ASM_SIZE_L);
    ops2(&s, op_post(2), op_post(3));
    ok("cmpm.l (%a2)+,(%a3)+", &s, B(0xB7, 0x8A), 2);
    s = insn(ASM_MN_CMPM, ASM_SIZE_B);
    ops2(&s, op_post(2), op_post(3));
    ok("cmpm.b (%a2)+,(%a3)+", &s, B(0xB7, 0x0A), 2);
    s = insn(ASM_MN_CMPM, ASM_SIZE_L);
    ops2(&s, op_mem(2), op_post(3));
    er("cmpm.l (%a2),(%a3)+ rejected", &s, ASM_ERR_BAD_OPERAND,
       ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_CMP, ASM_SIZE_B);
    ops2(&s, op_imm(5), op_dn(0));
    er("cmp.b #5,%d0 rejected (use cmpi)", &s, ASM_ERR_UNSUPPORTED,
       ASM_ERR_UNSUPPORTED);
    s = insn(ASM_MN_CMP, ASM_SIZE_W);
    ops2(&s, op_dn(0), op_mem(1));
    er("cmp.w %d0,(%a1) rejected (no Dn,<ea>)", &s, ASM_ERR_BAD_OPERAND,
       ASM_ERR_BAD_OPERAND);

    /* --- logic --- */
    s = insn(ASM_MN_AND, ASM_SIZE_W);
    ops2(&s, op_dn(1), op_dn(2));
    ok("and.w %d1,%d2", &s, B(0xC4, 0x41), 2);
    s = insn(ASM_MN_AND, ASM_SIZE_W);
    ops2(&s, op_dn(2), op_d16an(1, 8));
    ok("and.w %d2,8(%a1)", &s, B(0xC5, 0x69, 0x00, 0x08), 4);
    s = insn(ASM_MN_ANDI, ASM_SIZE_W);
    ops2(&s, op_imm(0x1234), op_dn(2));
    ok("andi.w #0x1234,%d2", &s, B(0x02, 0x42, 0x12, 0x34), 4);
    s = insn(ASM_MN_ANDI, ASM_SIZE_B);
    ops2(&s, op_imm(0x12), op_mem(1));
    ok("andi.b #0x12,(%a1)", &s, B(0x02, 0x11, 0x00, 0x12), 4);
    s = insn(ASM_MN_OR, ASM_SIZE_W);
    ops2(&s, op_dn(1), op_dn(2));
    ok("or.w %d1,%d2", &s, B(0x84, 0x41), 2);
    s = insn(ASM_MN_OR, ASM_SIZE_W);
    ops2(&s, op_dn(2), op_d16an(1, 8));
    ok("or.w %d2,8(%a1)", &s, B(0x85, 0x69, 0x00, 0x08), 4);
    s = insn(ASM_MN_ORI, ASM_SIZE_W);
    ops2(&s, op_imm(0x1234), op_dn(2));
    ok("ori.w #0x1234,%d2", &s, B(0x00, 0x42, 0x12, 0x34), 4);
    s = insn(ASM_MN_EOR, ASM_SIZE_W);
    ops2(&s, op_dn(1), op_dn(2));
    ok("eor.w %d1,%d2", &s, B(0xB3, 0x42), 2);
    s = insn(ASM_MN_EOR, ASM_SIZE_W);
    ops2(&s, op_dn(2), op_d16an(1, 8));
    ok("eor.w %d2,8(%a1)", &s, B(0xB5, 0x69, 0x00, 0x08), 4);
    s = insn(ASM_MN_EORI, ASM_SIZE_W);
    ops2(&s, op_imm(0x1234), op_dn(2));
    ok("eori.w #0x1234,%d2", &s, B(0x0A, 0x42, 0x12, 0x34), 4);
    s = insn(ASM_MN_EOR, ASM_SIZE_W);
    ops2(&s, op_mem(1), op_dn(0));
    er("eor.w (%a1),%d0 rejected", &s, ASM_ERR_BAD_OPERAND,
       ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_AND, ASM_SIZE_W);
    ops2(&s, op_an(0), op_dn(1));
    er("and.w %a0,%d1 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_AND, ASM_SIZE_W);
    ops2(&s, op_dn(0), op_an(1));
    er("and.w %d0,%a1 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_AND, ASM_SIZE_B);
    ops2(&s, op_imm(2), op_dn(0));
    er("and.b #2,%d0 rejected (use andi)", &s, ASM_ERR_UNSUPPORTED,
       ASM_ERR_UNSUPPORTED);

    /* --- mul/div/bcd/chk --- */
    s = insn(ASM_MN_MULS, ASM_SIZE_NONE);
    ops2(&s, op_dn(1), op_dn(2));
    ok("muls %d1,%d2", &s, B(0xC5, 0xC1), 2);
    s = insn(ASM_MN_MULU, ASM_SIZE_NONE);
    ops2(&s, op_mem(1), op_dn(2));
    ok("mulu (%a1),%d2", &s, B(0xC4, 0xD1), 2);
    s = insn(ASM_MN_MULU, ASM_SIZE_NONE);
    ops2(&s, op_imm(2), op_dn(0));
    ok("mulu #2,%d0", &s, B(0xC0, 0xFC, 0x00, 0x02), 4);
    s = insn(ASM_MN_DIVS, ASM_SIZE_NONE);
    ops2(&s, op_dn(1), op_dn(2));
    ok("divs %d1,%d2", &s, B(0x85, 0xC1), 2);
    s = insn(ASM_MN_DIVU, ASM_SIZE_NONE);
    ops2(&s, op_d16an(1, 8), op_dn(2));
    ok("divu 8(%a1),%d2", &s, B(0x84, 0xE9, 0x00, 0x08), 4);
    s = insn(ASM_MN_MULS, ASM_SIZE_W);
    ops2(&s, op_dn(1), op_dn(2));
    ok("muls.w %d1,%d2 (bare form is .w)", &s, B(0xC5, 0xC1), 2);
    s = insn(ASM_MN_ABCD, ASM_SIZE_NONE);
    ops2(&s, op_dn(1), op_dn(2));
    ok("abcd %d1,%d2", &s, B(0xC5, 0x01), 2);
    s = insn(ASM_MN_ABCD, ASM_SIZE_NONE);
    ops2(&s, op_pre(2), op_pre(3));
    ok("abcd -(%a2),-(%a3)", &s, B(0xC7, 0x0A), 2);
    s = insn(ASM_MN_SBCD, ASM_SIZE_NONE);
    ops2(&s, op_dn(1), op_dn(2));
    ok("sbcd %d1,%d2", &s, B(0x85, 0x01), 2);
    s = insn(ASM_MN_SBCD, ASM_SIZE_NONE);
    ops2(&s, op_pre(2), op_pre(3));
    ok("sbcd -(%a2),-(%a3)", &s, B(0x87, 0x0A), 2);
    s = insn(ASM_MN_ABCD, ASM_SIZE_B);
    ops2(&s, op_dn(1), op_dn(2));
    ok("abcd.b %d1,%d2 (accepted superset)", &s, B(0xC5, 0x01), 2);
    s = insn(ASM_MN_ABCD, ASM_SIZE_NONE);
    ops2(&s, op_mem(1), op_dn(2));
    er("abcd (%a1),%d2 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_CHK, ASM_SIZE_W);
    ops2(&s, op_d16an(1, 8), op_dn(2));
    ok("chk 8(%a1),%d2", &s, B(0x45, 0xA9, 0x00, 0x08), 4);
    s = insn(ASM_MN_CHK, ASM_SIZE_NONE);
    ops2(&s, op_dn(3), op_dn(1));
    ok("chk %d3,%d1", &s, B(0x43, 0x83), 2);
    s = insn(ASM_MN_CHK, ASM_SIZE_L);
    ops2(&s, op_dn(3), op_dn(1));
    er("chk.l rejected (68020+)", &s, ASM_ERR_BAD_SIZE, ASM_ERR_BAD_SIZE);

    /* --- shifts --- */
    s = insn(ASM_MN_ASL, ASM_SIZE_B);
    ops2(&s, op_imm(1), op_dn(0));
    ok("asl.b #1,%d0", &s, B(0xE3, 0x00), 2);
    s = insn(ASM_MN_ASL, ASM_SIZE_W);
    ops2(&s, op_imm(2), op_dn(0));
    ok("asl.w #2,%d0", &s, B(0xE5, 0x40), 2);
    s = insn(ASM_MN_ASL, ASM_SIZE_L);
    ops2(&s, op_imm(8), op_dn(0));
    ok("asl.l #8,%d0", &s, B(0xE1, 0x80), 2);
    s = insn(ASM_MN_ASL, ASM_SIZE_L);
    ops2(&s, op_dn(1), op_dn(0));
    ok("asl.l %d1,%d0", &s, B(0xE3, 0xA0), 2);
    s = insn(ASM_MN_ASR, ASM_SIZE_W);
    ops2(&s, op_imm(3), op_dn(5));
    ok("asr.w #3,%d5", &s, B(0xE6, 0x45), 2);
    s = insn(ASM_MN_LSR, ASM_SIZE_B);
    ops2(&s, op_imm(1), op_dn(0));
    ok("lsr.b #1,%d0", &s, B(0xE2, 0x08), 2);
    s = insn(ASM_MN_LSL, ASM_SIZE_L);
    ops2(&s, op_dn(2), op_dn(7));
    ok("lsl.l %d2,%d7", &s, B(0xE5, 0xAF), 2);
    s = insn(ASM_MN_ROL, ASM_SIZE_W);
    ops2(&s, op_imm(4), op_dn(3));
    ok("rol.w #4,%d3", &s, B(0xE9, 0x5B), 2);
    s = insn(ASM_MN_ROR, ASM_SIZE_B);
    ops2(&s, op_imm(1), op_dn(1));
    ok("ror.b #1,%d1", &s, B(0xE2, 0x19), 2);
    s = insn(ASM_MN_ROXL, ASM_SIZE_W);
    ops2(&s, op_imm(2), op_dn(0));
    ok("roxl.w #2,%d0", &s, B(0xE5, 0x50), 2);
    s = insn(ASM_MN_ROXR, ASM_SIZE_L);
    ops2(&s, op_imm(7), op_dn(4));
    ok("roxr.l #7,%d4", &s, B(0xEE, 0x94), 2);
    s = insn(ASM_MN_ASL, ASM_SIZE_NONE);
    ops(&s, op_mem(1));
    ok("asl (%a1)", &s, B(0xE1, 0xD1), 2);
    s = insn(ASM_MN_ASL, ASM_SIZE_W);
    ops(&s, op_d16an(1, 8));
    ok("asl 8(%a1)", &s, B(0xE1, 0xE9, 0x00, 0x08), 4);
    s = insn(ASM_MN_LSR, ASM_SIZE_NONE);
    ops(&s, op_mem(1));
    ok("lsr (%a1)", &s, B(0xE2, 0xD1), 2);
    s = insn(ASM_MN_ROL, ASM_SIZE_NONE);
    ops(&s, op_abs_w(0x1234));
    ok("rol 0x1234.w", &s, B(0xE7, 0xF8, 0x12, 0x34), 4);
    s = insn(ASM_MN_ASL, ASM_SIZE_L);
    ops2(&s, op_imm(9), op_dn(0));
    er("asl.l #9 out of range", &s, ASM_ERR_RANGE, ASM_OK);
    s = insn(ASM_MN_ASL, ASM_SIZE_L);
    ops2(&s, op_imm(1), op_an(0));
    er("asl.l #1,%a0 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_ASL, ASM_SIZE_B);
    ops(&s, op_mem(1));
    er("asl.b (%a1) rejected (memory is word)", &s, ASM_ERR_BAD_SIZE,
       ASM_ERR_BAD_SIZE);
    s = insn(ASM_MN_ASL, ASM_SIZE_L);
    ops(&s, op_dn(0));
    er("asl.l %d0 rejected (count or memory)", &s, ASM_ERR_OPERAND_COUNT,
       ASM_ERR_OPERAND_COUNT);

    /* --- bit operations --- */
    s = insn(ASM_MN_BSET, ASM_SIZE_NONE);
    ops2(&s, op_imm(0), op_dn(0));
    ok("bset #0,%d0", &s, B(0x08, 0xC0, 0x00, 0x00), 4);
    s = insn(ASM_MN_BSET, ASM_SIZE_NONE);
    ops2(&s, op_imm(7), op_d16an(1, 8));
    ok("bset #7,8(%a1)", &s, B(0x08, 0xE9, 0x00, 0x07, 0x00, 0x08), 6);
    s = insn(ASM_MN_BSET, ASM_SIZE_NONE);
    ops2(&s, op_dn(1), op_dn(0));
    ok("bset %d1,%d0", &s, B(0x03, 0xC0), 2);
    s = insn(ASM_MN_BSET, ASM_SIZE_NONE);
    ops2(&s, op_dn(1), op_mem(1));
    ok("bset %d1,(%a1)", &s, B(0x03, 0xD1), 2);
    s = insn(ASM_MN_BCLR, ASM_SIZE_NONE);
    ops2(&s, op_imm(3), op_dn(2));
    ok("bclr #3,%d2", &s, B(0x08, 0x82, 0x00, 0x03), 4);
    s = insn(ASM_MN_BCLR, ASM_SIZE_NONE);
    ops2(&s, op_dn(3), op_mem(1));
    ok("bclr %d3,(%a1)", &s, B(0x07, 0x91), 2);
    s = insn(ASM_MN_BCHG, ASM_SIZE_NONE);
    ops2(&s, op_imm(1), op_dn(4));
    ok("bchg #1,%d4", &s, B(0x08, 0x44, 0x00, 0x01), 4);
    s = insn(ASM_MN_BCHG, ASM_SIZE_NONE);
    ops2(&s, op_dn(5), op_mem(1));
    ok("bchg %d5,(%a1)", &s, B(0x0B, 0x51), 2);
    s = insn(ASM_MN_BTST, ASM_SIZE_NONE);
    ops2(&s, op_imm(2), op_dn(6));
    ok("btst #2,%d6", &s, B(0x08, 0x06, 0x00, 0x02), 4);
    s = insn(ASM_MN_BTST, ASM_SIZE_NONE);
    ops2(&s, op_imm(15), op_dn(0));
    ok("btst #15,%d0", &s, B(0x08, 0x00, 0x00, 0x0F), 4);
    s = insn(ASM_MN_BTST, ASM_SIZE_NONE);
    ops2(&s, op_dn(7), op_dn(0));
    ok("btst %d7,%d0", &s, B(0x0F, 0x00), 2);
    s = insn(ASM_MN_BTST, ASM_SIZE_NONE);
    ops2(&s, op_imm(3), op_d16an(1, 8));
    ok("btst #3,8(%a1)", &s, B(0x08, 0x29, 0x00, 0x03, 0x00, 0x08), 6);
    s = insn(ASM_MN_BTST, ASM_SIZE_NONE);
    ops2(&s, op_imm(3), op_post(1));
    ok("btst #3,(%a1)+", &s, B(0x08, 0x19, 0x00, 0x03), 4);
    s = insn(ASM_MN_BTST, ASM_SIZE_B);
    ops2(&s, op_imm(3), op_dn(0));
    ok("btst.b #3,%d0 (bare form is .b)", &s, B(0x08, 0x00, 0x00, 0x03), 4);
    s = insn(ASM_MN_BSET, ASM_SIZE_NONE);
    ops2(&s, op_dn(1), op_pc(4));
    er("bset %d1,4(%pc) rejected", &s, ASM_ERR_BAD_OPERAND,
       ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_BTST, ASM_SIZE_NONE);
    ops2(&s, op_imm(2), op_an(0));
    er("btst #2,%a0 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_BTST, ASM_SIZE_NONE);
    ops2(&s, op_imm(32), op_dn(0));
    er("btst #32 out of range", &s, ASM_ERR_RANGE, ASM_OK);

    /* --- branches --- */
    s = insn(ASM_MN_BRA, ASM_SIZE_NONE);
    ops(&s, op_abs_expr(ex_id("tlbl")));
    ok("bra tlbl (default .w)", &s, B(0x60, 0x00, 0x00, 0xFE), 4);
    s = insn(ASM_MN_BRA, ASM_SIZE_W);
    ops(&s, op_abs_expr(ex_id("tlbl")));
    ok("bra.w tlbl", &s, B(0x60, 0x00, 0x00, 0xFE), 4);
    s = insn(ASM_MN_BRA, ASM_SIZE_B);
    ops(&s, op_abs_expr(ex_id("tnext")));
    ok("bra.b tnext", &s, B(0x60, 0x3E), 2);
    s = insn(ASM_MN_BSR, ASM_SIZE_W);
    ops(&s, op_abs_expr(ex_id("tlbl")));
    ok("bsr tlbl", &s, B(0x61, 0x00, 0x00, 0xFE), 4);
    s = insn_cc(ASM_MN_BEQ, ASM_SIZE_W, ASM_CC_EQ);
    ops(&s, op_abs_expr(ex_id("tlbl")));
    ok("beq tlbl", &s, B(0x67, 0x00, 0x00, 0xFE), 4);
    s = insn_cc(ASM_MN_BCS, ASM_SIZE_B, ASM_CC_CS);
    ops(&s, op_abs_expr(ex_id("tnext")));
    ok("bcs.b tnext", &s, B(0x65, 0x3E), 2);
    s = insn(ASM_MN_BRA, ASM_SIZE_B);
    ops(&s, op_abs_expr(ex_id("tback")));
    ok("bra.b tback (backward)", &s, B(0x60, 0x0E), 2);
    s = insn(ASM_MN_BRA, ASM_SIZE_B);
    ops(&s, op_abs_expr(ex_id("absym")));
    er("bra.b absym rejected (not relocatable)", &s, ASM_ERR_NOT_ABSOLUTE,
       ASM_OK);
    s = insn(ASM_MN_BRA, ASM_SIZE_L);
    ops(&s, op_abs_expr(ex_id("tlbl")));
    er("bra.l rejected", &s, ASM_ERR_BAD_SIZE, ASM_ERR_BAD_SIZE);
    s = insn(ASM_MN_BRA, ASM_SIZE_NONE);
    ops(&s, op_mem(1));
    er("bra (%a1) rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_BRA, ASM_SIZE_B);
    ops(&s, op_abs_expr(ex_id("xtrn")));
    er("bra.b xtrn rejected", &s, ASM_ERR_UNSUPPORTED, ASM_OK);
    s = insn(ASM_MN_BRA, ASM_SIZE_W);
    ops(&s, op_abs_expr(ex_id("xtrn")));
    ok_reloc("bra.w xtrn -> PC16", &s, B(0x60, 0x00, 0x00, 0x00), 4,
             ASM_RELOC_PC16, 2, "xtrn", 0);
    s = insn(ASM_MN_BRA, ASM_SIZE_W);
    ops(&s, op_abs_expr(ex_id("dlbl")));
    ok_reloc("bra.w dlbl -> PC16 (cross-section)", &s,
             B(0x60, 0x00, 0x00, 0x20), 4, ASM_RELOC_PC16, 2, "\001data", 0x20);

    /* --- dbcc --- */
    s = insn(ASM_MN_DBRA, ASM_SIZE_NONE);
    ops2(&s, op_dn(0), op_abs_expr(ex_id("tlbl")));
    ok("dbra %d0,tlbl", &s, B(0x51, 0xC8, 0x00, 0xFE), 4);
    s = insn_cc(ASM_MN_DBEQ, ASM_SIZE_NONE, ASM_CC_EQ);
    ops2(&s, op_dn(1), op_abs_expr(ex_id("tlbl")));
    ok("dbeq %d1,tlbl", &s, B(0x57, 0xC9, 0x00, 0xFE), 4);
    s = insn_cc(ASM_MN_DBEQ, ASM_SIZE_W, ASM_CC_EQ);
    ops2(&s, op_dn(1), op_abs_expr(ex_id("tlbl")));
    ok("dbeq.w %d1,tlbl (bare form is .w)", &s, B(0x57, 0xC9, 0x00, 0xFE), 4);
    s = insn(ASM_MN_DBRA, ASM_SIZE_NONE);
    ops2(&s, op_dn(0), op_abs_expr(ex_id("xtrn")));
    ok_reloc("dbra %d0,xtrn -> PC16", &s, B(0x51, 0xC8, 0x00, 0x00), 4,
             ASM_RELOC_PC16, 2, "xtrn", 0);
    s = insn(ASM_MN_DBRA, ASM_SIZE_NONE);
    ops2(&s, op_an(0), op_abs_expr(ex_id("tlbl")));
    er("dbra %a0 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);

    /* --- scc --- */
    s = insn_cc(ASM_MN_SEQ, ASM_SIZE_NONE, ASM_CC_EQ);
    ops(&s, op_dn(0));
    ok("seq %d0", &s, B(0x57, 0xC0), 2);
    s = insn_cc(ASM_MN_SNE, ASM_SIZE_B, ASM_CC_NE);
    ops(&s, op_mem(1));
    ok("sne.b (%a1)", &s, B(0x56, 0xD1), 2);
    s = insn_cc(ASM_MN_SCS, ASM_SIZE_NONE, ASM_CC_CS);
    ops(&s, op_d16an(1, 8));
    ok("scs 8(%a1)", &s, B(0x55, 0xE9, 0x00, 0x08), 4);
    s = insn_cc(ASM_MN_SCC, ASM_SIZE_NONE, ASM_CC_CC);
    ops(&s, op_mem(1));
    ok("scc (%a1)", &s, B(0x54, 0xD1), 2);
    s = insn_cc(ASM_MN_SEQ, ASM_SIZE_NONE, ASM_CC_EQ);
    ops(&s, op_an(0));
    er("seq %a0 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn_cc(ASM_MN_SEQ, ASM_SIZE_L, ASM_CC_EQ);
    ops(&s, op_dn(0));
    er("seq.l rejected", &s, ASM_ERR_BAD_SIZE, ASM_ERR_BAD_SIZE);

    /* --- jmp/jsr and simple instructions --- */
    s = insn(ASM_MN_JMP, ASM_SIZE_NONE);
    ops(&s, op_d16an(1, 8));
    ok("jmp 8(%a1)", &s, B(0x4E, 0xE9, 0x00, 0x08), 4);
    s = insn(ASM_MN_JMP, ASM_SIZE_NONE);
    ops(&s, op_abs(0x12345678u));
    ok("jmp 0x12345678", &s, B(0x4E, 0xF9, 0x12, 0x34, 0x56, 0x78), 6);
    s = insn(ASM_MN_JSR, ASM_SIZE_NONE);
    ops(&s, op_mem(1));
    ok("jsr (%a1)", &s, B(0x4E, 0x91), 2);
    s = insn(ASM_MN_JSR, ASM_SIZE_NONE);
    ops(&s, op_ix(0, 1, 2, 1, 8));
    ok("jsr 8(%a1,%d2.l)", &s, B(0x4E, 0xB1, 0x28, 0x08), 4);
    s = insn(ASM_MN_JMP, ASM_SIZE_NONE);
    ops(&s, op_post(1));
    er("jmp (%a1)+ rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_JSR, ASM_SIZE_NONE);
    ops(&s, op_imm(4));
    er("jsr #4 rejected", &s, ASM_ERR_BAD_OPERAND, ASM_ERR_BAD_OPERAND);
    s = insn(ASM_MN_NOP, ASM_SIZE_NONE);
    ok("nop", &s, B(0x4E, 0x71), 2);
    s = insn(ASM_MN_RTE, ASM_SIZE_NONE);
    ok("rte", &s, B(0x4E, 0x73), 2);
    s = insn(ASM_MN_RTS, ASM_SIZE_NONE);
    ok("rts", &s, B(0x4E, 0x75), 2);
    s = insn(ASM_MN_RTR, ASM_SIZE_NONE);
    ok("rtr", &s, B(0x4E, 0x77), 2);
    s = insn(ASM_MN_TRAPV, ASM_SIZE_NONE);
    ok("trapv", &s, B(0x4E, 0x76), 2);
    s = insn(ASM_MN_RESET, ASM_SIZE_NONE);
    ok("reset", &s, B(0x4E, 0x70), 2);
    s = insn(ASM_MN_ILLEGAL, ASM_SIZE_NONE);
    ok("illegal", &s, B(0x4A, 0xFC), 2);
    s = insn(ASM_MN_NOP, ASM_SIZE_B);
    er("nop.b rejected", &s, ASM_ERR_BAD_SIZE, ASM_ERR_BAD_SIZE);
    s = insn(ASM_MN_STOP, ASM_SIZE_NONE);
    ops(&s, op_imm(0x2700));
    ok("stop #0x2700", &s, B(0x4E, 0x72, 0x27, 0x00), 4);
    s = insn(ASM_MN_STOP, ASM_SIZE_NONE);
    ops(&s, op_imm(0x10000));
    er("stop #0x10000 out of range", &s, ASM_ERR_RANGE, ASM_OK);
    s = insn(ASM_MN_TRAP, ASM_SIZE_NONE);
    ops(&s, op_imm(0));
    ok("trap #0", &s, B(0x4E, 0x40), 2);
    s = insn(ASM_MN_TRAP, ASM_SIZE_NONE);
    ops(&s, op_imm(15));
    ok("trap #15", &s, B(0x4E, 0x4F), 2);
    s = insn(ASM_MN_TRAP, ASM_SIZE_NONE);
    ops(&s, op_imm(16));
    er("trap #16 out of range", &s, ASM_ERR_RANGE, ASM_OK);

    /* --- relocations: absolute addressing and immediates --- */
    s = insn(ASM_MN_MOVE, ASM_SIZE_L);
    ops2(&s, op_abs_expr(ex_id("xtrn")), op_dn(0));
    ok_reloc("move.l xtrn,%d0 -> ABS32", &s,
             B(0x20, 0x39, 0x00, 0x00, 0x00, 0x00), 6, ASM_RELOC_ABS32, 2,
             "xtrn", 0);
    s = insn(ASM_MN_MOVE, ASM_SIZE_L);
    ops2(&s, op_abs_expr(ex_id("dlbl")), op_dn(0));
    ok_reloc("move.l dlbl,%d0 -> ABS32 section", &s,
             B(0x20, 0x39, 0x00, 0x00, 0x00, 0x20), 6, ASM_RELOC_ABS32, 2,
             "\001data", 0x20);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_abs_expr(ex_id("dlbl")), op_dn(0));
    ok_reloc("move.w dlbl,%d0 -> abs.l ABS32", &s,
             B(0x30, 0x39, 0x00, 0x00, 0x00, 0x20), 6, ASM_RELOC_ABS32, 2,
             "\001data", 0x20);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_abs_w_expr(ex_id("dlbl")), op_dn(0));
    ok_reloc("move.w dlbl.w,%d0 -> ABS16", &s, B(0x30, 0x38, 0x00, 0x20), 4,
             ASM_RELOC_ABS16, 2, "\001data", 0x20);
    s = insn(ASM_MN_MOVE, ASM_SIZE_L);
    ops2(&s, op_abs_w_expr(ex_id("xtrn")), op_dn(0));
    ok_reloc("move.l xtrn.w,%d0 -> ABS16", &s, B(0x20, 0x38, 0x00, 0x00), 4,
             ASM_RELOC_ABS16, 2, "xtrn", 0);
    s = insn(ASM_MN_MOVE, ASM_SIZE_L);
    ops2(&s, op_imm_expr(ex_id("xtrn")), op_dn(0));
    ok_reloc("move.l #xtrn,%d0 -> ABS32", &s,
             B(0x20, 0x3C, 0x00, 0x00, 0x00, 0x00), 6, ASM_RELOC_ABS32, 2,
             "xtrn", 0);
    s = insn(ASM_MN_MOVE, ASM_SIZE_L);
    ops2(&s, op_imm_expr(ex_id("dlbl")), op_dn(0));
    ok_reloc("move.l #dlbl,%d0 -> ABS32 section", &s,
             B(0x20, 0x3C, 0x00, 0x00, 0x00, 0x20), 6, ASM_RELOC_ABS32, 2,
             "\001data", 0x20);
    s = insn(ASM_MN_CMPI, ASM_SIZE_W);
    ops2(&s, op_imm_expr(ex_id("xtrn")), op_dn(0));
    ok_reloc("cmpi.w #xtrn,%d0 -> ABS16", &s, B(0x0C, 0x40, 0x00, 0x00), 4,
             ASM_RELOC_ABS16, 2, "xtrn", 0);
    s = insn(ASM_MN_CMPI, ASM_SIZE_B);
    ops2(&s, op_imm_expr(ex_id("xtrn")), op_mem(1));
    ok_reloc("cmpi.b #xtrn,(%a1) -> ABS8 at value byte", &s,
             B(0x0C, 0x11, 0x00, 0x00), 4, ASM_RELOC_ABS8, 3, "xtrn", 0);
    s = insn(ASM_MN_MOVE, ASM_SIZE_B);
    ops2(&s, op_imm_expr(ex_id("xtrn")), op_dn(0));
    ok_reloc("move.b #xtrn,%d0 -> ABS8 at value byte", &s,
             B(0x10, 0x3C, 0x00, 0x00), 4, ASM_RELOC_ABS8, 3, "xtrn", 0);
    s = insn(ASM_MN_LEA, ASM_SIZE_NONE);
    ops2(&s, op_mem_expr(0, ex_id("dlbl")), op_an(1));
    ok_reloc("lea dlbl(%a0),%a1 -> ABS16", &s, B(0x43, 0xE8, 0x00, 0x20), 4,
             ASM_RELOC_ABS16, 2, "\001data", 0x20);
    s = insn(ASM_MN_JSR, ASM_SIZE_NONE);
    ops(&s, op_abs_expr(ex_id("tlbl")));
    ok_reloc("jsr tlbl -> ABS32 same section", &s,
             B(0x4E, 0xB9, 0x00, 0x00, 0x01, 0x00), 6, ASM_RELOC_ABS32, 2,
             "\001text", 0x100);

    /* --- relocations: PC-relative forms --- */
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_pc_expr(ex_id("tlbl")), op_dn(0));
    ok("move.w tlbl(%pc),%d0 resolved", &s, B(0x30, 0x3A, 0x00, 0xFE), 4);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_pc_expr(ex_id("xtrn")), op_dn(0));
    ok_reloc("move.w xtrn(%pc),%d0 -> PC16", &s, B(0x30, 0x3A, 0x00, 0x00), 4,
             ASM_RELOC_PC16, 2, "xtrn", 0);
    s = insn(ASM_MN_LEA, ASM_SIZE_NONE);
    ops2(&s, op_pc_expr(ex_id_off("tlbl", 0, 4)), op_an(2));
    ok("lea tlbl+4(%pc),%a2 resolved", &s, B(0x45, 0xFA, 0x01, 0x02), 4);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_ix(1, 0, 3, 1, 4), op_dn(0));
    ok("move.w 4(%pc,%d3.l),%d0 resolved", &s, B(0x30, 0x3B, 0x38, 0x04), 4);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_ix_expr(1, 0, 3, 1, ex_id("xtrn")), op_dn(0));
    /* GAS emits R_68K_PC8 for this form; S9O1 has no PC8 relocation, so
     * a symbol displacement would corrupt the brief extension word */
    er("move.w xtrn(%pc,%d3.l) rejected", &s, ASM_ERR_NOT_ABSOLUTE, ASM_OK);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_ix_expr(0, 0, 3, 1, ex_id("xtrn")), op_dn(0));
    er("move.w xtrn(%a0,%d3.l),%d0 not absolute", &s, ASM_ERR_NOT_ABSOLUTE,
       ASM_OK);
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_pc_expr(ex_id("absym")), op_dn(0));
    /* absolute constants keep the literal displacement (GAS parity:
     * move.w absym(%pc),%d0 encodes 303a 1234) */
    ok("move.w absym(%pc) literal", &s, B(0x30, 0x3A, 0x12, 0x34), 4);

    /* --- range checks --- */
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_imm(0x10000), op_dn(0));
    er("move.w #0x10000 out of range", &s, ASM_ERR_RANGE, ASM_OK);
    s = insn(ASM_MN_MOVE, ASM_SIZE_B);
    ops2(&s, op_imm(256), op_dn(0));
    er("move.b #256 out of range", &s, ASM_ERR_RANGE, ASM_OK);
    s = insn(ASM_MN_MOVE, ASM_SIZE_B);
    ops2(&s, op_imm(0xFFFFFF80u), op_dn(0));
    ok("move.b #-128,%d0", &s, B(0x10, 0x3C, 0xFF, 0x80), 4);
    s = insn(ASM_MN_LEA, ASM_SIZE_NONE);
    ops2(&s, op_mem_expr(0, ex_num(0x8000u)), op_an(1));
    er("lea 0x8000(%a0) out of range", &s, ASM_ERR_RANGE, ASM_OK);
    s = insn(ASM_MN_LEA, ASM_SIZE_NONE);
    ops2(&s, op_mem_expr(0, ex_num(0x80000000u)), op_an(1));
    er("lea d32(%a0) out of range", &s, ASM_ERR_RANGE, ASM_OK);

    /* --- undefined symbol surfaces from the evaluator --- */
    s = insn(ASM_MN_MOVE, ASM_SIZE_W);
    ops2(&s, op_abs_expr(ex_id("nosuch")), op_dn(0));
    er("move.w nosuch,%d0 undefined", &s, ASM_ERR_UNDEFINED_SYMBOL, ASM_OK);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
