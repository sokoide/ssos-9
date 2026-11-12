/* Host unit tests for the assembler lexer, parser, and expression
 * evaluator (apps/asm/lex.c, apps/asm/parse.c). */

#include "asm.h"
#include <stdio.h>
#include <stdlib.h>

static int checks;
static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                      \
    } while (0)

static AsmToken toks[ASM_TOKEN_MAX];
static uint8_t tok_count;

static AsmStatus lex(const char* line) {
    return asm_lex(line, toks, ASM_TOKEN_MAX, &tok_count);
}

static uint8_t kind_at(uint8_t i) {
    if (i > tok_count)
        return ASM_TOK_EOL;
    return toks[i].kind;
}

/* Lexes line, parses it, and returns the status; stmt receives the
 * statement. */
static AsmStatus parse_line(const char* line, AsmStmt* stmt) {
    AsmStatus status = lex(line);
    if (status != ASM_OK)
        return status;
    return asm_parse(toks, tok_count, stmt);
}

/* Builds an AsmExpr from an expression-only source string. */
static AsmStatus expr_of(const char* text, AsmExpr* expr) {
    uint8_t i;
    AsmStatus status = lex(text);
    if (status != ASM_OK)
        return status;
    expr->count = tok_count;
    for (i = 0; i < tok_count; i++) expr->ops[i] = toks[i];
    return ASM_OK;
}

static AsmStatus eval_line(const AsmCtx* ctx, const char* text, AsmValue* out) {
    AsmExpr expr;
    AsmStatus status = expr_of(text, &expr);
    if (status != ASM_OK)
        return status;
    return asm_expr_eval(ctx, &expr, out);
}

static void test_lexer_tokens(void) {
    CHECK(lex("move.l %d0, %a0") == ASM_OK);
    /* "move.l" is a single identifier: the size suffix is part of the
     * ident token text (split by the parser). */
    CHECK(tok_count == 4);
    CHECK(kind_at(0) == ASM_TOK_IDENT && toks[0].length == 6);
    CHECK(kind_at(1) == ASM_TOK_REG && toks[1].reg == ASM_REG_D0);
    CHECK(kind_at(2) == ASM_TOK_COMMA);
    CHECK(kind_at(3) == ASM_TOK_REG && toks[3].reg == ASM_REG_A0);
    CHECK(kind_at(4) == ASM_TOK_EOL);

    CHECK(lex("+ - * / & | ^ ~ ( ) , : #") == ASM_OK);
    CHECK(tok_count == 13);
    CHECK(kind_at(0) == ASM_TOK_PLUS && kind_at(1) == ASM_TOK_MINUS);
    CHECK(kind_at(2) == ASM_TOK_STAR && kind_at(3) == ASM_TOK_SLASH);
    CHECK(kind_at(4) == ASM_TOK_AND && kind_at(5) == ASM_TOK_OR);
    CHECK(kind_at(6) == ASM_TOK_XOR && kind_at(7) == ASM_TOK_NOT);
    CHECK(kind_at(8) == ASM_TOK_LPAREN && kind_at(9) == ASM_TOK_RPAREN);
    CHECK(kind_at(10) == ASM_TOK_COMMA && kind_at(11) == ASM_TOK_COLON);
    CHECK(kind_at(12) == ASM_TOK_HASH);
    CHECK(kind_at(13) == ASM_TOK_EOL);
}

static void test_lexer_registers(void) {
    CHECK(lex("%sp") == ASM_OK && toks[0].reg == ASM_REG_A7);
    CHECK(lex("%a7") == ASM_OK && toks[0].reg == ASM_REG_A7);
    CHECK(lex("%pc") == ASM_OK && toks[0].reg == ASM_REG_PC);
    CHECK(lex("%ccr") == ASM_OK && toks[0].reg == ASM_REG_CCR);
    CHECK(lex("%sr") == ASM_OK && toks[0].reg == ASM_REG_SR);
    CHECK(lex("%usp") == ASM_OK && toks[0].reg == ASM_REG_USP);
    CHECK(lex("%D3") == ASM_OK && toks[0].reg == ASM_REG_D3);
    CHECK(lex("%a6") == ASM_OK && toks[0].reg == ASM_REG_A6);
    CHECK(lex("%xyz") == ASM_ERR_UNKNOWN_REGISTER);
    CHECK(lex("%fp") == ASM_ERR_UNKNOWN_REGISTER);
    CHECK(lex("%d8") == ASM_ERR_UNKNOWN_REGISTER);
    CHECK(lex("%") == ASM_ERR_UNKNOWN_REGISTER);
}

static void test_lexer_numbers(void) {
    CHECK(lex("123") == ASM_OK);
    CHECK(toks[0].kind == ASM_TOK_NUMBER && toks[0].number == 123);
    CHECK(lex("$FC") == ASM_OK && toks[0].number == 0xFC);
    CHECK(lex("$fc") == ASM_OK && toks[0].number == 0xFC);
    CHECK(lex("0xFC") == ASM_OK && toks[0].number == 0xFC);
    CHECK(lex("0Xab") == ASM_OK && toks[0].number == 0xAB);
    CHECK(lex("'A") == ASM_OK && toks[0].kind == ASM_TOK_NUMBER &&
          toks[0].number == 65);
    CHECK(lex("' ") == ASM_OK && toks[0].number == 32);
    CHECK(lex("'$") == ASM_OK && toks[0].number == 36);
    CHECK(lex("$") == ASM_ERR_SYNTAX);
    CHECK(lex("0x") == ASM_ERR_SYNTAX);
    CHECK(lex("'") == ASM_ERR_SYNTAX);
    /* numeric label references stay idents */
    CHECK(lex("3b") == ASM_OK && toks[0].kind == ASM_TOK_IDENT &&
          toks[0].length == 2);
    CHECK(lex("9f") == ASM_OK && toks[0].kind == ASM_TOK_IDENT &&
          toks[0].text[0] == '9');
    CHECK(lex("12b") == ASM_OK);
    CHECK(toks[0].kind == ASM_TOK_NUMBER && toks[0].number == 12);
    CHECK(toks[1].kind == ASM_TOK_IDENT && toks[1].length == 1);
}

static void test_lexer_idents_comments(void) {
    CHECK(lex("foo_1") == ASM_OK);
    CHECK(toks[0].kind == ASM_TOK_IDENT && toks[0].length == 5);
    CHECK(lex(".") == ASM_OK && toks[0].kind == ASM_TOK_IDENT &&
          toks[0].length == 1);
    CHECK(lex(".dc.b") == ASM_OK && toks[0].kind == ASM_TOK_IDENT &&
          toks[0].length == 5);
    CHECK(lex("foo ; rest is comment") == ASM_OK && tok_count == 1);
    CHECK(lex("; only a comment") == ASM_OK && tok_count == 0);
    CHECK(lex("'; not a comment") == ASM_OK);
    CHECK(toks[0].kind == ASM_TOK_NUMBER && toks[0].number == (uint8_t)';');
    /* over-long identifier */
    {
        char line[64];
        int i;
        for (i = 0; i < 40; i++) line[i] = 'a';
        line[40] = 0;
        CHECK(lex(line) == ASM_ERR_SYNTAX);
    }
}

static void test_lexer_strings_errors(void) {
    CHECK(lex("\"ab\\nc\"") == ASM_OK);
    CHECK(toks[0].kind == ASM_TOK_STRING && toks[0].length == 5);
    CHECK(toks[0].text[0] == 'a' && toks[0].text[1] == 'b' &&
          toks[0].text[2] == '\\');
    CHECK(lex("\"unterminated") == ASM_ERR_SYNTAX);
    CHECK(lex("<") == ASM_ERR_SYNTAX);
    CHECK(lex(">") == ASM_ERR_SYNTAX);
    CHECK(lex("@") == ASM_ERR_SYNTAX);
    CHECK(lex("1 << 3") == ASM_OK && kind_at(1) == ASM_TOK_SHL);
    CHECK(lex("1 >> 3") == ASM_OK && kind_at(1) == ASM_TOK_SHR);
    /* token overflow */
    {
        char line[ASM_LINE_MAX];
        int i;
        for (i = 0; i < 60; i++) {
            line[i * 2] = '1';
            line[i * 2 + 1] = '+';
        }
        line[120] = 0;
        CHECK(lex(line) == ASM_ERR_SYNTAX);
    }
}

static void test_mnemonic_lookup(void) {
    uint8_t mn = ASM_MN_NONE;
    uint8_t cc = 0;
    CHECK(asm_mnemonic_lookup("move", 4, &mn, &cc) && mn == ASM_MN_MOVE &&
          cc == 0);
    CHECK(asm_mnemonic_lookup("MOVE", 4, &mn, &cc) && mn == ASM_MN_MOVE);
    CHECK(asm_mnemonic_lookup("illegal", 7, &mn, &cc) && mn == ASM_MN_ILLEGAL);
    CHECK(asm_mnemonic_lookup("unlink", 6, &mn, &cc) && mn == ASM_MN_UNLINK);
    CHECK(asm_mnemonic_lookup("movem", 5, &mn, &cc) && mn == ASM_MN_MOVEM);
    CHECK(asm_mnemonic_lookup("bne", 3, &mn, &cc) && mn == ASM_MN_BNE &&
          cc == ASM_CC_NE);
    CHECK(asm_mnemonic_lookup("bgt", 3, &mn, &cc) && mn == ASM_MN_BGT &&
          cc == ASM_CC_GT);
    CHECK(asm_mnemonic_lookup("bra", 3, &mn, &cc) && mn == ASM_MN_BRA &&
          cc == 0);
    CHECK(asm_mnemonic_lookup("dbra", 4, &mn, &cc) && mn == ASM_MN_DBRA &&
          cc == 0);
    CHECK(asm_mnemonic_lookup("dbeq", 4, &mn, &cc) && mn == ASM_MN_DBEQ &&
          cc == ASM_CC_EQ);
    CHECK(asm_mnemonic_lookup("scc", 3, &mn, &cc) && mn == ASM_MN_SCC &&
          cc == ASM_CC_CC);
    CHECK(asm_mnemonic_lookup("sls", 3, &mn, &cc) && mn == ASM_MN_SLS &&
          cc == ASM_CC_LS);
    CHECK(asm_mnemonic_lookup("trapv", 5, &mn, &cc) && mn == ASM_MN_TRAPV);
    CHECK(!asm_mnemonic_lookup("st", 2, &mn, &cc));
    CHECK(!asm_mnemonic_lookup("sf", 2, &mn, &cc));
    CHECK(!asm_mnemonic_lookup("movl", 4, &mn, &cc));
    CHECK(!asm_mnemonic_lookup("foo", 3, &mn, &cc));
}

static void test_parse_labels(void) {
    AsmStmt stmt;
    CHECK(parse_line("start:", &stmt) == ASM_OK);
    CHECK(stmt.kind == ASM_STMT_EMPTY);
    CHECK(stmt.label_len == 5 && stmt.label[0] == 's');
    CHECK(parse_line("1:", &stmt) == ASM_OK);
    CHECK(stmt.kind == ASM_STMT_EMPTY && stmt.label_len == 1 &&
          stmt.label[0] == '1');
    CHECK(parse_line("12:", &stmt) == ASM_ERR_SYNTAX);
    CHECK(parse_line("3: nop", &stmt) == ASM_OK);
    CHECK(stmt.kind == ASM_STMT_INSN && stmt.label_len == 1 &&
          stmt.label[0] == '3');
    CHECK(stmt.mnemonic == ASM_MN_NOP);
    CHECK(parse_line("; nothing", &stmt) == ASM_OK &&
          stmt.kind == ASM_STMT_EMPTY);
    CHECK(parse_line("", &stmt) == ASM_OK && stmt.kind == ASM_STMT_EMPTY);
    CHECK(parse_line("1 nop", &stmt) == ASM_ERR_SYNTAX);
}

static void test_parse_sizes(void) {
    AsmStmt stmt;
    CHECK(parse_line("move.l %d0,%a0", &stmt) == ASM_OK);
    CHECK(stmt.mnemonic == ASM_MN_MOVE && stmt.size == ASM_SIZE_L &&
          stmt.op_count == 2);
    CHECK(parse_line("move %d0,%a0", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("bne.s loop", &stmt) == ASM_OK);
    CHECK(stmt.mnemonic == ASM_MN_BNE && stmt.cc == ASM_CC_NE &&
          stmt.size == ASM_SIZE_B);
    CHECK(parse_line("bra loop", &stmt) == ASM_OK && stmt.size == ASM_SIZE_W);
    CHECK(parse_line("bne.b loop", &stmt) == ASM_OK && stmt.size == ASM_SIZE_B);
    CHECK(parse_line("bra.l loop", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("scc %d0", &stmt) == ASM_OK && stmt.size == ASM_SIZE_B);
    CHECK(parse_line("seq %d0", &stmt) == ASM_OK && stmt.cc == ASM_CC_EQ);
    CHECK(parse_line("scc.w %d0", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("scc.s %d0", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("dbra %d0,loop", &stmt) == ASM_OK &&
          stmt.size == ASM_SIZE_W);
    CHECK(parse_line("dbra.b %d0,loop", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("ext.w %d0", &stmt) == ASM_OK && stmt.size == ASM_SIZE_W);
    CHECK(parse_line("ext.b %d0", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("ext %d0", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("swap.l %d0", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("swap %d0", &stmt) == ASM_OK &&
          stmt.size == ASM_SIZE_NONE);
    CHECK(parse_line("abcd.b %d0,%d1", &stmt) == ASM_OK &&
          stmt.size == ASM_SIZE_B);
    CHECK(parse_line("abcd %d0,%d1", &stmt) == ASM_OK &&
          stmt.size == ASM_SIZE_B);
    CHECK(parse_line("muls.w %d0,%d1", &stmt) == ASM_OK &&
          stmt.size == ASM_SIZE_W);
    CHECK(parse_line("muls.l %d0,%d1", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("muls %d0,%d1", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("movea.w %a0,%a1", &stmt) == ASM_OK);
    CHECK(parse_line("movea.b %a0,%a1", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("movem.l %d0,-(%sp)", &stmt) == ASM_OK);
    CHECK(parse_line("movem.b %d0,-(%sp)", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("tst.b (%a0)", &stmt) == ASM_OK);
    CHECK(parse_line("tas.b (%a0)", &stmt) == ASM_OK &&
          stmt.size == ASM_SIZE_B);
    CHECK(parse_line("tas %d0", &stmt) == ASM_OK && stmt.size == ASM_SIZE_B);
    CHECK(parse_line("tas.w (%a0)", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("bset.b #0,%d0", &stmt) == ASM_OK &&
          stmt.size == ASM_SIZE_B);
    CHECK(parse_line("bset #0,%d0", &stmt) == ASM_OK &&
          stmt.size == ASM_SIZE_B);
    CHECK(parse_line("bset.w #0,%d0", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("moveq #4,%d0", &stmt) == ASM_OK &&
          stmt.size == ASM_SIZE_NONE);
    CHECK(parse_line("moveq.l #4,%d0", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("lea foo,%a0", &stmt) == ASM_OK &&
          stmt.size == ASM_SIZE_NONE);
    CHECK(parse_line("lea.l foo,%a0", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("move.l.x %d0,%a0", &stmt) == ASM_ERR_BAD_SIZE);
    CHECK(parse_line("nop", &stmt) == ASM_OK && stmt.op_count == 0);
    CHECK(parse_line("foo %d0", &stmt) == ASM_ERR_UNKNOWN_MNEMONIC);
    CHECK(parse_line("foo", &stmt) == ASM_ERR_UNKNOWN_MNEMONIC);
}

static void test_parse_operands(void) {
    AsmStmt stmt;
    CHECK(parse_line("move.l #4,%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_IMM && stmt.ops[0].expr.count == 1);
    CHECK(stmt.ops[0].expr.ops[0].kind == ASM_TOK_NUMBER &&
          stmt.ops[0].expr.ops[0].number == 4);
    CHECK(stmt.ops[1].mode == ASM_OP_REG && stmt.ops[1].base_reg == ASM_REG_D0);

    CHECK(parse_line("move.b (%a0)+,%d7", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_MEM_POSTINC &&
          stmt.ops[0].base_reg == ASM_REG_A0);
    CHECK(stmt.ops[1].base_reg == ASM_REG_D7);

    CHECK(parse_line("move.w -(%sp),%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_MEM_PREDEC &&
          stmt.ops[0].base_reg == ASM_REG_A7);

    CHECK(parse_line("move.l 8(%a0),%d1", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_MEM && stmt.ops[0].base_reg == ASM_REG_A0);
    CHECK(stmt.ops[0].expr.count == 1 && stmt.ops[0].expr.ops[0].number == 8);

    CHECK(parse_line("move.l (%pc),%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_MEM && stmt.ops[0].base_reg == ASM_REG_PC);
    CHECK(stmt.ops[0].expr.count == 0);

    CHECK(parse_line("move.w 4(%pc,%d2),%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_INDEX &&
          stmt.ops[0].base_reg == ASM_REG_PC);
    CHECK(stmt.ops[0].index_reg == ASM_REG_D2 && stmt.ops[0].index_long == 1);

    CHECK(parse_line("move.w 4(%a0,%d3.w),%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_INDEX && stmt.ops[0].index_long == 0);

    CHECK(parse_line("move.l (%a1,%a2.l),%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_INDEX &&
          stmt.ops[0].base_reg == ASM_REG_A1);
    CHECK(stmt.ops[0].index_reg == ASM_REG_A2 && stmt.ops[0].index_long == 1);

    CHECK(parse_line("jmp foo", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_ABS && stmt.ops[0].expr.count == 1);
    CHECK(stmt.ops[0].expr.ops[0].kind == ASM_TOK_IDENT);

    CHECK(parse_line("move.l #'.',%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_IMM &&
          stmt.ops[0].expr.ops[0].number == 46);

    CHECK(parse_line("stop #$2700", &stmt) == ASM_OK);
    CHECK(stmt.mnemonic == ASM_MN_STOP && stmt.ops[0].mode == ASM_OP_IMM);
    CHECK(stmt.ops[0].expr.ops[0].number == 0x2700);

    CHECK(parse_line("move.l %d0,%a0 %a1", &stmt) == ASM_ERR_SYNTAX);
    CHECK(parse_line("move.l 4(%a0)+,%d0", &stmt) == ASM_ERR_BAD_OPERAND);
    CHECK(parse_line("move.l (%pc)+,%d0", &stmt) == ASM_ERR_BAD_OPERAND);
    CHECK(parse_line("move.l (%d0),%d1", &stmt) == ASM_ERR_BAD_OPERAND);
    CHECK(parse_line("move.l 4(%a0,%pc),%d0", &stmt) == ASM_ERR_BAD_OPERAND);
    CHECK(parse_line("move.l ,%d0", &stmt) == ASM_ERR_SYNTAX);
}

static void test_parse_abs_size(void) {
    AsmStmt stmt;
    /* "4.w" lexes as NUMBER + ".w": the suffix leaves the expression */
    CHECK(parse_line("move.l 4.w,%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_ABS && stmt.ops[0].abs_size == ASM_SIZE_W);
    CHECK(stmt.ops[0].expr.count == 1 &&
          stmt.ops[0].expr.ops[0].kind == ASM_TOK_NUMBER &&
          stmt.ops[0].expr.ops[0].number == 4);
    CHECK(parse_line("move.l 4.l,%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].abs_size == ASM_SIZE_L && stmt.ops[0].expr.count == 1);
    /* omitted suffix: abs_size 0 (encodes as .l per contract) */
    CHECK(parse_line("move.l 4,%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_ABS && stmt.ops[0].abs_size == 0);
    /* "lbl.w" lexes as one identifier; only the name part is kept */
    CHECK(parse_line("move.l lbl.w,%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].abs_size == ASM_SIZE_W);
    CHECK(stmt.ops[0].expr.count == 1 &&
          stmt.ops[0].expr.ops[0].kind == ASM_TOK_IDENT &&
          stmt.ops[0].expr.ops[0].length == 3);
    CHECK(parse_line("move.l lbl.l,%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].abs_size == ASM_SIZE_L &&
          stmt.ops[0].expr.ops[0].length == 3);
    /* only the trailing component is a suffix: "a.b" stays one name */
    CHECK(parse_line("move.l a.b,%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].abs_size == 0 && stmt.ops[0].expr.ops[0].length == 3);
    CHECK(parse_line("move.l a.b.w,%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].abs_size == ASM_SIZE_W &&
          stmt.ops[0].expr.ops[0].length == 3);
    /* source and destination operands carry their own suffix */
    CHECK(parse_line("move.w %d0,0x1234.w", &stmt) == ASM_OK);
    CHECK(stmt.ops[1].mode == ASM_OP_ABS && stmt.ops[1].abs_size == ASM_SIZE_W);
    /* control-mode and one-operand forms */
    CHECK(parse_line("jsr foo.w", &stmt) == ASM_OK &&
          stmt.ops[0].abs_size == ASM_SIZE_W);
    CHECK(parse_line("lea foo.w,%a0", &stmt) == ASM_OK &&
          stmt.ops[0].abs_size == ASM_SIZE_W);
    CHECK(parse_line("pea 4.l", &stmt) == ASM_OK &&
          stmt.ops[0].abs_size == ASM_SIZE_L);
    /* parenthesized absolute form ending the operand list */
    CHECK(parse_line("jmp (4).w", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_ABS && stmt.ops[0].abs_size == ASM_SIZE_W);
    /* a bare ".w"/".l" leaves no expression: syntax error */
    CHECK(parse_line("move.l .w,%d0", &stmt) == ASM_ERR_SYNTAX);
    CHECK(parse_line("move.l .l,%d0", &stmt) == ASM_ERR_SYNTAX);
    /* branch targets are absolute operands too */
    CHECK(parse_line("bra.w lbl.w", &stmt) == ASM_OK &&
          stmt.ops[0].mode == ASM_OP_ABS && stmt.ops[0].abs_size == ASM_SIZE_W);
}

static void test_parse_reglist(void) {
    AsmStmt stmt;
    CHECK(parse_line("movem.l %d0-%d3/%a0,-(%sp)", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_REGLIST && stmt.ops[0].reg_mask == 0x010F);
    CHECK(parse_line("movem.l %d0/%d1/%a6-%a7,(%a0)", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].reg_mask == 0xC003 && stmt.ops[1].mode == ASM_OP_MEM);
    CHECK(parse_line("movem.l %a0/%d3,%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].reg_mask == (uint16_t)((1u << 8) | (1u << 3)));
    CHECK(parse_line("movem.l %d3-%d0,%d0", &stmt) == ASM_ERR_BAD_OPERAND);
    CHECK(parse_line("movem.l %d0-%a3,%d0", &stmt) == ASM_ERR_BAD_OPERAND);
    CHECK(parse_line("movem.l %d0-,%d0", &stmt) == ASM_ERR_BAD_OPERAND);
    CHECK(parse_line("movem.l %ccr/%d0,%d1", &stmt) == ASM_ERR_BAD_OPERAND);
    /* a lone special register parses as a plain register operand;
     * rejecting it for movem is the encoder's job */
    CHECK(parse_line("movem.l %ccr,%d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_REG &&
          stmt.ops[0].base_reg == ASM_REG_CCR);
}

static void test_parse_directives(void) {
    AsmStmt stmt;
    CHECK(parse_line(".text", &stmt) == ASM_OK);
    CHECK(stmt.kind == ASM_STMT_SECTION && stmt.section == ASM_SECTION_TEXT);
    CHECK(parse_line(".data", &stmt) == ASM_OK &&
          stmt.section == ASM_SECTION_DATA);
    CHECK(parse_line(".bss", &stmt) == ASM_OK &&
          stmt.section == ASM_SECTION_BSS);
    CHECK(parse_line(".text extra", &stmt) == ASM_ERR_SYNTAX);

    CHECK(parse_line(".globl foo", &stmt) == ASM_OK);
    CHECK(stmt.kind == ASM_STMT_GLOBAL && stmt.name_len == 3 &&
          stmt.name[0] == 'f');
    CHECK(parse_line(".extern bar", &stmt) == ASM_OK &&
          stmt.kind == ASM_STMT_EXTERN);
    CHECK(parse_line(".globl", &stmt) == ASM_ERR_BAD_OPERAND);
    CHECK(parse_line(".globl 5", &stmt) == ASM_ERR_BAD_OPERAND);
    CHECK(parse_line(".globl foo bar", &stmt) == ASM_ERR_SYNTAX);

    CHECK(parse_line(".equ X, 4+2", &stmt) == ASM_OK);
    CHECK(stmt.kind == ASM_STMT_EQU && stmt.name_len == 1 &&
          stmt.name[0] == 'X');
    CHECK(stmt.expr.count == 3);

    CHECK(parse_line(".byte 1,2,3,4,5,6,7,8", &stmt) == ASM_OK);
    CHECK(stmt.kind == ASM_STMT_DATA && stmt.size == ASM_SIZE_B &&
          stmt.value_count == 8);
    CHECK(parse_line(".dc.w 1,$10", &stmt) == ASM_OK);
    CHECK(stmt.size == ASM_SIZE_W && stmt.value_count == 2);
    CHECK(stmt.values[1].ops[0].number == 0x10);
    CHECK(parse_line(".dc.l foo", &stmt) == ASM_OK && stmt.size == ASM_SIZE_L);
    CHECK(parse_line(".byte 1,2,3,4,5,6,7,8,9", &stmt) == ASM_ERR_SYNTAX);
    CHECK(parse_line(".byte", &stmt) == ASM_ERR_SYNTAX);

    CHECK(parse_line(".space 10", &stmt) == ASM_OK);
    CHECK(stmt.kind == ASM_STMT_SPACE && stmt.expr.count == 1);
    CHECK(stmt.ops[0].base_reg == 1);
    CHECK(parse_line(".ds.b 3", &stmt) == ASM_OK && stmt.ops[0].base_reg == 1);
    CHECK(parse_line(".ds.w 3", &stmt) == ASM_OK && stmt.ops[0].base_reg == 2);
    CHECK(parse_line(".ds.l 3", &stmt) == ASM_OK && stmt.ops[0].base_reg == 4);

    CHECK(parse_line(".even", &stmt) == ASM_OK && stmt.kind == ASM_STMT_EVEN);
    CHECK(parse_line(".end", &stmt) == ASM_OK && stmt.kind == ASM_STMT_END);
    CHECK(parse_line(".even x", &stmt) == ASM_ERR_SYNTAX);
    CHECK(parse_line(".foobar", &stmt) == ASM_ERR_UNKNOWN_DIRECTIVE);
    CHECK(parse_line(".foobar x", &stmt) == ASM_ERR_UNKNOWN_DIRECTIVE);
}

static void test_parse_ascii(void) {
    AsmStmt stmt;
    CHECK(parse_line(".asciz \"hi\\n\"", &stmt) == ASM_OK);
    CHECK(stmt.kind == ASM_STMT_ASCII && stmt.string_zero == 1);
    CHECK(stmt.string_len == 3);
    CHECK(stmt.string[0] == 'h' && stmt.string[1] == 'i' &&
          stmt.string[2] == '\n');
    CHECK(parse_line(".ascii \"a\\\\b\"", &stmt) == ASM_OK);
    CHECK(stmt.string_zero == 0 && stmt.string_len == 3);
    CHECK(stmt.string[0] == 'a' && stmt.string[1] == '\\' &&
          stmt.string[2] == 'b');
    CHECK(parse_line(".ascii \"x\\0y\"", &stmt) == ASM_OK);
    CHECK(stmt.string_len == 3 && stmt.string[1] == '\0');
    CHECK(parse_line(".ascii \"q\\r\\t\"", &stmt) == ASM_OK);
    CHECK(stmt.string_len == 3 && stmt.string[0] == 'q' &&
          stmt.string[1] == '\r' && stmt.string[2] == '\t');
    CHECK(parse_line(".ascii 5", &stmt) == ASM_ERR_BAD_OPERAND);
    CHECK(parse_line(".ascii \"a\",\"b\"", &stmt) == ASM_ERR_BAD_OPERAND);
    CHECK(parse_line(".ascii \"a\\qb\"", &stmt) == ASM_ERR_BAD_OPERAND);
    CHECK(parse_line(".ascii \"a\\bb\"", &stmt) == ASM_ERR_BAD_OPERAND);
}

static void test_expr_setup_ctx(AsmCtx* ctx) {
    asm_ctx_init(ctx);
    ctx->pass = 2;
    ctx->line = 5;
    ctx->section = ASM_SECTION_TEXT;
    ctx->offset[ASM_SECTION_TEXT] = 0x10;
    ctx->offset[ASM_SECTION_DATA] = 0x40;
}

static void test_expr_eval_abs(void) {
    AsmCtx ctx;
    AsmValue v;
    test_expr_setup_ctx(&ctx);
    CHECK(eval_line(&ctx, "5", &v) == ASM_OK && v.kind == ASM_VAL_ABS &&
          v.value == 5);
    CHECK(eval_line(&ctx, "2+3*4", &v) == ASM_OK && v.value == 14);
    CHECK(eval_line(&ctx, "(2+3)*4", &v) == ASM_OK && v.value == 20);
    CHECK(eval_line(&ctx, "20/6", &v) == ASM_OK && (int32_t)v.value == 3);
    CHECK(eval_line(&ctx, "-20/6", &v) == ASM_OK && (int32_t)v.value == -3);
    CHECK(eval_line(&ctx, "1<<3", &v) == ASM_OK && v.value == 8);
    CHECK(eval_line(&ctx, "256>>4", &v) == ASM_OK && v.value == 16);
    /* contract: shift amount 0 is treated as 1 */
    CHECK(eval_line(&ctx, "1<<0", &v) == ASM_OK && v.value == 2);
    CHECK(eval_line(&ctx, "5>>0", &v) == ASM_OK && v.value == 2);
    CHECK(eval_line(&ctx, "1<<32", &v) == ASM_ERR_RANGE);
    CHECK(eval_line(&ctx, "1<<-1", &v) == ASM_ERR_RANGE);
    CHECK(eval_line(&ctx, "5/0", &v) == ASM_ERR_RANGE);
    CHECK(eval_line(&ctx, "~0", &v) == ASM_OK && (int32_t)v.value == -1);
    CHECK(eval_line(&ctx, "-(2+3)", &v) == ASM_OK && (int32_t)v.value == -5);
    /* precedence: & above ^ above | */
    CHECK(eval_line(&ctx, "1|2^3&6", &v) == ASM_OK && v.value == 1);
    CHECK(eval_line(&ctx, "1|6", &v) == ASM_OK && v.value == 7);
    CHECK(eval_line(&ctx, "1+2<<2", &v) == ASM_OK && v.value == 12);
    CHECK(eval_line(&ctx, "$10+0x10", &v) == ASM_OK && v.value == 32);
}

static void test_expr_eval_rel(void) {
    AsmCtx ctx;
    AsmValue v;
    test_expr_setup_ctx(&ctx);
    CHECK(asm_sym_define(&ctx, "foo", 3, ASM_SECTION_TEXT, 0x20) == ASM_OK);
    CHECK(asm_sym_define(&ctx, "datsym", 6, ASM_SECTION_DATA, 0x50) == ASM_OK);
    CHECK(asm_sym_define(&ctx, "N", 1, ASM_SECTION_ABS, 7) == ASM_OK);
    CHECK(eval_line(&ctx, ".", &v) == ASM_OK);
    CHECK(v.kind == ASM_VAL_REL && v.section == ASM_SECTION_TEXT &&
          v.value == 0x10);
    CHECK(eval_line(&ctx, "foo", &v) == ASM_OK);
    CHECK(v.kind == ASM_VAL_REL && v.section == ASM_SECTION_TEXT &&
          v.value == 0x20);
    CHECK(eval_line(&ctx, "foo+4", &v) == ASM_OK && v.kind == ASM_VAL_REL &&
          v.value == 0x24);
    CHECK(eval_line(&ctx, "4+foo", &v) == ASM_OK && v.kind == ASM_VAL_REL &&
          v.value == 0x24);
    CHECK(eval_line(&ctx, "foo-4", &v) == ASM_OK && v.kind == ASM_VAL_REL &&
          v.value == 0x1C);
    CHECK(eval_line(&ctx, "4-foo", &v) == ASM_ERR_NOT_ABSOLUTE);
    CHECK(eval_line(&ctx, "foo+foo", &v) == ASM_ERR_NOT_ABSOLUTE);
    CHECK(eval_line(&ctx, "foo-bar", &v) == ASM_ERR_UNDEFINED_SYMBOL);
    CHECK(eval_line(&ctx, "foo-.", &v) == ASM_OK && v.kind == ASM_VAL_ABS &&
          v.value == 0x10);
    CHECK(eval_line(&ctx, ".-foo", &v) == ASM_OK && v.kind == ASM_VAL_ABS);
    CHECK((int32_t)v.value == -0x10);
    CHECK(eval_line(&ctx, "foo-datsym", &v) == ASM_ERR_NOT_ABSOLUTE);
    CHECK(eval_line(&ctx, "foo*2", &v) == ASM_ERR_NOT_ABSOLUTE);
    CHECK(eval_line(&ctx, "foo&1", &v) == ASM_ERR_NOT_ABSOLUTE);
    CHECK(eval_line(&ctx, "foo<<1", &v) == ASM_ERR_NOT_ABSOLUTE);
    CHECK(eval_line(&ctx, "-foo", &v) == ASM_ERR_NOT_ABSOLUTE);
    CHECK(eval_line(&ctx, "N", &v) == ASM_OK && v.kind == ASM_VAL_ABS &&
          v.value == 7);
    CHECK(eval_line(&ctx, "N*2", &v) == ASM_OK && v.value == 14);
    CHECK(eval_line(&ctx, "zzz", &v) == ASM_ERR_UNDEFINED_SYMBOL);
}

static void test_expr_eval_numeric(void) {
    AsmCtx ctx;
    AsmValue v;
    test_expr_setup_ctx(&ctx);
    ctx.line = 1;
    CHECK(asm_numeric_define(&ctx, 3) == ASM_OK); /* text offset 0x10, line 1 */
    ctx.offset[ASM_SECTION_TEXT] = 0x30;
    ctx.line = 9;
    CHECK(asm_numeric_define(&ctx, 3) == ASM_OK); /* text offset 0x30, line 9 */
    ctx.line = 5;
    CHECK(eval_line(&ctx, "3b", &v) == ASM_OK);
    CHECK(v.kind == ASM_VAL_REL && v.section == ASM_SECTION_TEXT &&
          v.value == 0x10);
    CHECK(eval_line(&ctx, "3f", &v) == ASM_OK);
    CHECK(v.kind == ASM_VAL_REL && v.value == 0x30);
    CHECK(eval_line(&ctx, "3b+2", &v) == ASM_OK && v.value == 0x12);
    CHECK(eval_line(&ctx, "7b", &v) == ASM_ERR_UNDEFINED_SYMBOL);
    CHECK(eval_line(&ctx, "7f", &v) == ASM_ERR_UNDEFINED_SYMBOL);
}

static void test_expr_eval_extern(void) {
    AsmCtx ctx;
    AsmValue v;
    test_expr_setup_ctx(&ctx);
    CHECK(asm_sym_extern(&ctx, "ext", 3) == ASM_OK);
    CHECK(eval_line(&ctx, "ext", &v) == ASM_OK);
    CHECK(v.kind == ASM_VAL_EXTERN && v.symbol == 0 && v.value == 0);
    CHECK(eval_line(&ctx, "ext+8", &v) == ASM_OK);
    CHECK(v.kind == ASM_VAL_EXTERN && (int32_t)v.value == 8);
    CHECK(eval_line(&ctx, "ext-4", &v) == ASM_OK);
    CHECK(v.kind == ASM_VAL_EXTERN && (int32_t)v.value == -4);
    CHECK(eval_line(&ctx, "ext*2", &v) == ASM_ERR_NOT_ABSOLUTE);
    CHECK(eval_line(&ctx, "ext+ext", &v) == ASM_ERR_NOT_ABSOLUTE);
    CHECK(eval_line(&ctx, "-ext", &v) == ASM_ERR_NOT_ABSOLUTE);
    CHECK(eval_line(&ctx, "8-ext", &v) == ASM_ERR_NOT_ABSOLUTE);
    /* empty expression evaluates to absolute 0 */
    {
        AsmExpr expr;
        expr.count = 0;
        CHECK(asm_expr_eval(&ctx, &expr, &v) == ASM_OK);
        CHECK(v.kind == ASM_VAL_ABS && v.value == 0);
    }
}

static void test_parse_insn_from_sources(void) {
    /* forms taken from tests/asm/t01_hello.s style sources */
    AsmStmt stmt;
    CHECK(parse_line("move.l #0x4000, %sp", &stmt) == ASM_OK);
    CHECK(stmt.mnemonic == ASM_MN_MOVE && stmt.size == ASM_SIZE_L);
    CHECK(stmt.ops[1].mode == ASM_OP_REG && stmt.ops[1].base_reg == ASM_REG_A7);
    CHECK(parse_line("move.b #'', %d0", &stmt) == ASM_OK);
    CHECK(parse_line("lea msg, %a0", &stmt) == ASM_OK);
    CHECK(stmt.mnemonic == ASM_MN_LEA && stmt.ops[0].mode == ASM_OP_ABS);
    CHECK(parse_line("beq done", &stmt) == ASM_OK && stmt.cc == ASM_CC_EQ);
    CHECK(parse_line("move.b (%a0)+, %d0", &stmt) == ASM_OK);
    CHECK(parse_line("addq.l #1, %d1", &stmt) == ASM_OK);
    CHECK(parse_line("cmp.l #4, %d1", &stmt) == ASM_OK);
    CHECK(parse_line("blt count_loop", &stmt) == ASM_OK &&
          stmt.cc == ASM_CC_LT);
    CHECK(parse_line("link %a6,#-16", &stmt) == ASM_OK);
    CHECK(stmt.mnemonic == ASM_MN_LINK && stmt.op_count == 2);
    CHECK(parse_line("move.w %sr, %d0", &stmt) == ASM_OK);
    CHECK(stmt.ops[0].mode == ASM_OP_REG && stmt.ops[0].base_reg == ASM_REG_SR);
    CHECK(parse_line("move.l %a0,%ccr", &stmt) == ASM_OK);
    CHECK(stmt.ops[1].base_reg == ASM_REG_CCR);
}

int main(void) {
    test_lexer_tokens();
    test_lexer_registers();
    test_lexer_numbers();
    test_lexer_idents_comments();
    test_lexer_strings_errors();
    test_mnemonic_lookup();
    test_parse_labels();
    test_parse_sizes();
    test_parse_operands();
    test_parse_abs_size();
    test_parse_reglist();
    test_parse_directives();
    test_parse_ascii();
    test_expr_eval_abs();
    test_expr_eval_rel();
    test_expr_eval_numeric();
    test_expr_eval_extern();
    test_parse_insn_from_sources();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
