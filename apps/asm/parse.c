/* Parser for the SSOS-9 MC68000 assembler: mnemonic table, statement
 * and operand parsing, directives, and expression evaluation per
 * docs/assembler-contract.md. */

#include "asm.h"

static char lower(char c) {
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

/* Case-insensitive compare of name[0..len) against a NUL-terminated
 * constant string. */
static int name_ieq(const char* name, uint8_t len, const char* str) {
    uint8_t i;
    for (i = 0; i < len; i++) {
        if (str[i] == 0)
            return 0;
        if (lower(name[i]) != lower(str[i]))
            return 0;
    }
    return str[len] == 0;
}

/* --- Mnemonic table ------------------------------------------------ */

/* Fixed-size name storage keeps the table free of pointer relocations:
 * S9M1 modules carry code only (no allocated data sections). */
#define MN_NAME_MAX 8 /* "illegal" is the longest mnemonic */
typedef struct {
    char name[MN_NAME_MAX];
    uint8_t mnemonic;
    uint8_t cc;
} MnemonicEntry;

static const MnemonicEntry mnemonic_table[] = {
    {"abcd", ASM_MN_ABCD, 0},         {"add", ASM_MN_ADD, 0},
    {"adda", ASM_MN_ADDA, 0},         {"addi", ASM_MN_ADDI, 0},
    {"addq", ASM_MN_ADDQ, 0},         {"and", ASM_MN_AND, 0},
    {"andi", ASM_MN_ANDI, 0},         {"asl", ASM_MN_ASL, 0},
    {"asr", ASM_MN_ASR, 0},           {"bcc", ASM_MN_BCC, ASM_CC_CC},
    {"bcs", ASM_MN_BCS, ASM_CC_CS},   {"beq", ASM_MN_BEQ, ASM_CC_EQ},
    {"bge", ASM_MN_BGE, ASM_CC_GE},   {"bgt", ASM_MN_BGT, ASM_CC_GT},
    {"bhi", ASM_MN_BHI, ASM_CC_HI},   {"ble", ASM_MN_BLE, ASM_CC_LE},
    {"bls", ASM_MN_BLS, ASM_CC_LS},   {"blt", ASM_MN_BLT, ASM_CC_LT},
    {"bmi", ASM_MN_BMI, ASM_CC_MI},   {"bne", ASM_MN_BNE, ASM_CC_NE},
    {"bpl", ASM_MN_BPL, ASM_CC_PL},   {"bvc", ASM_MN_BVC, ASM_CC_VC},
    {"bvs", ASM_MN_BVS, ASM_CC_VS},   {"bra", ASM_MN_BRA, 0},
    {"bsr", ASM_MN_BSR, 0},           {"bchg", ASM_MN_BCHG, 0},
    {"bclr", ASM_MN_BCLR, 0},         {"bset", ASM_MN_BSET, 0},
    {"btst", ASM_MN_BTST, 0},         {"chk", ASM_MN_CHK, 0},
    {"clr", ASM_MN_CLR, 0},           {"cmp", ASM_MN_CMP, 0},
    {"cmpa", ASM_MN_CMPA, 0},         {"cmpi", ASM_MN_CMPI, 0},
    {"cmpm", ASM_MN_CMPM, 0},         {"dbcc", ASM_MN_DBCC, ASM_CC_CC},
    {"dbcs", ASM_MN_DBCS, ASM_CC_CS}, {"dbeq", ASM_MN_DBEQ, ASM_CC_EQ},
    {"dbge", ASM_MN_DBGE, ASM_CC_GE}, {"dbgt", ASM_MN_DBGT, ASM_CC_GT},
    {"dbhi", ASM_MN_DBHI, ASM_CC_HI}, {"dble", ASM_MN_DBLE, ASM_CC_LE},
    {"dbls", ASM_MN_DBLS, ASM_CC_LS}, {"dblt", ASM_MN_DBLT, ASM_CC_LT},
    {"dbmi", ASM_MN_DBMI, ASM_CC_MI}, {"dbne", ASM_MN_DBNE, ASM_CC_NE},
    {"dbpl", ASM_MN_DBPL, ASM_CC_PL}, {"dbvc", ASM_MN_DBVC, ASM_CC_VC},
    {"dbvs", ASM_MN_DBVS, ASM_CC_VS}, {"dbra", ASM_MN_DBRA, 0},
    {"divs", ASM_MN_DIVS, 0},         {"divu", ASM_MN_DIVU, 0},
    {"eor", ASM_MN_EOR, 0},           {"eori", ASM_MN_EORI, 0},
    {"exg", ASM_MN_EXG, 0},           {"ext", ASM_MN_EXT, 0},
    {"illegal", ASM_MN_ILLEGAL, 0},   {"jmp", ASM_MN_JMP, 0},
    {"jsr", ASM_MN_JSR, 0},           {"lea", ASM_MN_LEA, 0},
    {"link", ASM_MN_LINK, 0},         {"lsl", ASM_MN_LSL, 0},
    {"lsr", ASM_MN_LSR, 0},           {"move", ASM_MN_MOVE, 0},
    {"movea", ASM_MN_MOVEA, 0},       {"movem", ASM_MN_MOVEM, 0},
    {"moveq", ASM_MN_MOVEQ, 0},       {"movep", ASM_MN_MOVEP, 0},
    {"muls", ASM_MN_MULS, 0},         {"mulu", ASM_MN_MULU, 0},
    {"nbcd", ASM_MN_NBCD, 0},         {"neg", ASM_MN_NEG, 0},
    {"negx", ASM_MN_NEGX, 0},         {"nop", ASM_MN_NOP, 0},
    {"not", ASM_MN_NOT, 0},           {"or", ASM_MN_OR, 0},
    {"ori", ASM_MN_ORI, 0},           {"pea", ASM_MN_PEA, 0},
    {"reset", ASM_MN_RESET, 0},       {"rol", ASM_MN_ROL, 0},
    {"ror", ASM_MN_ROR, 0},           {"roxl", ASM_MN_ROXL, 0},
    {"roxr", ASM_MN_ROXR, 0},         {"rtr", ASM_MN_RTR, 0},
    {"rte", ASM_MN_RTE, 0},           {"rts", ASM_MN_RTS, 0},
    {"sbcd", ASM_MN_SBCD, 0},         {"scc", ASM_MN_SCC, ASM_CC_CC},
    {"scs", ASM_MN_SCS, ASM_CC_CS},   {"seq", ASM_MN_SEQ, ASM_CC_EQ},
    {"sge", ASM_MN_SGE, ASM_CC_GE},   {"sgt", ASM_MN_SGT, ASM_CC_GT},
    {"shi", ASM_MN_SHI, ASM_CC_HI},   {"sle", ASM_MN_SLE, ASM_CC_LE},
    {"slt", ASM_MN_SLT, ASM_CC_LT},   {"sls", ASM_MN_SLS, ASM_CC_LS},
    {"smi", ASM_MN_SMI, ASM_CC_MI},   {"sne", ASM_MN_SNE, ASM_CC_NE},
    {"spl", ASM_MN_SPL, ASM_CC_PL},   {"svc", ASM_MN_SVC, ASM_CC_VC},
    {"svs", ASM_MN_SVS, ASM_CC_VS},   {"stop", ASM_MN_STOP, 0},
    {"sub", ASM_MN_SUB, 0},           {"suba", ASM_MN_SUBA, 0},
    {"subi", ASM_MN_SUBI, 0},         {"subq", ASM_MN_SUBQ, 0},
    {"swap", ASM_MN_SWAP, 0},         {"tas", ASM_MN_TAS, 0},
    {"trap", ASM_MN_TRAP, 0},         {"trapv", ASM_MN_TRAPV, 0},
    {"tst", ASM_MN_TST, 0},           {"unlink", ASM_MN_UNLINK, 0},
    {"unlk", ASM_MN_UNLINK, 0} /* GAS spelling */
};

int asm_mnemonic_lookup(const char* name, uint8_t len, uint8_t* mnemonic,
                        uint8_t* cc) {
    uint32_t i;
    uint32_t table_count = sizeof(mnemonic_table) / sizeof(mnemonic_table[0]);
    for (i = 0; i < table_count; i++) {
        if (name_ieq(name, len, mnemonic_table[i].name)) {
            *mnemonic = mnemonic_table[i].mnemonic;
            *cc = mnemonic_table[i].cc;
            return 1;
        }
    }
    return 0;
}

/* --- Size suffix policy -------------------------------------------- */

enum {
    SGRP_FORBIDDEN = 0, /* no suffix allowed (size fixed/none) */
    SGRP_BWL_REQ,       /* .b/.w/.l required */
    SGRP_BWL_OPT,       /* optional .b/.w/.l, default .w (shifts, GAS parity) */
    SGRP_WL_REQ,        /* .w/.l required */
    SGRP_W_REQ,         /* .w required */
    SGRP_B_OPT,         /* optional .b, default .b */
    SGRP_L_REQ,         /* .l required */
    SGRP_SCC,           /* optional .b, default .b */
    SGRP_BRANCH,        /* optional .b/.s/.w, default .w */
    SGRP_DB             /* optional .w, default .w */
};

static int size_group(uint8_t mn) {
    switch (mn) {
    case ASM_MN_MOVE:
    case ASM_MN_ADD:
    case ASM_MN_ADDI:
    case ASM_MN_ADDQ:
    case ASM_MN_SUB:
    case ASM_MN_SUBI:
    case ASM_MN_SUBQ:
    case ASM_MN_AND:
    case ASM_MN_ANDI:
    case ASM_MN_OR:
    case ASM_MN_ORI:
    case ASM_MN_EOR:
    case ASM_MN_EORI:
    case ASM_MN_CMP:
    case ASM_MN_CMPI:
    case ASM_MN_CMPM:
    case ASM_MN_NEG:
    case ASM_MN_NEGX:
    case ASM_MN_NOT:
    case ASM_MN_CLR:
    case ASM_MN_TST: return SGRP_BWL_REQ;
    case ASM_MN_ASL:
    case ASM_MN_ASR:
    case ASM_MN_LSL:
    case ASM_MN_LSR:
    case ASM_MN_ROL:
    case ASM_MN_ROR:
    case ASM_MN_ROXL:
    case ASM_MN_ROXR:
        return SGRP_BWL_OPT; /* bare defaults .w (verified against GAS) */
    case ASM_MN_MOVEA:
    case ASM_MN_MOVEM:
    case ASM_MN_MOVEP:
    case ASM_MN_ADDA:
    case ASM_MN_SUBA:
    case ASM_MN_CMPA:
    case ASM_MN_EXT: return SGRP_WL_REQ;
    case ASM_MN_CHK: return SGRP_W_REQ; /* chk.l is 68020+, out of scope */
    case ASM_MN_MULS:
    case ASM_MN_MULU:
    case ASM_MN_DIVS:
    case ASM_MN_DIVU: return SGRP_W_REQ;
    case ASM_MN_TAS:
    case ASM_MN_ABCD:
    case ASM_MN_SBCD:
    case ASM_MN_NBCD:
    case ASM_MN_BCHG:
    case ASM_MN_BCLR:
    case ASM_MN_BSET:
    case ASM_MN_BTST: return SGRP_B_OPT;
    case ASM_MN_SWAP: return SGRP_FORBIDDEN;
    case ASM_MN_SCC:
    case ASM_MN_SCS:
    case ASM_MN_SEQ:
    case ASM_MN_SGE:
    case ASM_MN_SGT:
    case ASM_MN_SHI:
    case ASM_MN_SLE:
    case ASM_MN_SLS:
    case ASM_MN_SMI:
    case ASM_MN_SNE:
    case ASM_MN_SPL:
    case ASM_MN_SVC:
    case ASM_MN_SVS: return SGRP_SCC;
    case ASM_MN_BCC:
    case ASM_MN_BCS:
    case ASM_MN_BEQ:
    case ASM_MN_BGE:
    case ASM_MN_BGT:
    case ASM_MN_BHI:
    case ASM_MN_BLE:
    case ASM_MN_BLS:
    case ASM_MN_BLT:
    case ASM_MN_BMI:
    case ASM_MN_BNE:
    case ASM_MN_BPL:
    case ASM_MN_BVC:
    case ASM_MN_BVS:
    case ASM_MN_BRA:
    case ASM_MN_BSR: return SGRP_BRANCH;
    case ASM_MN_DBCC:
    case ASM_MN_DBCS:
    case ASM_MN_DBEQ:
    case ASM_MN_DBGE:
    case ASM_MN_DBGT:
    case ASM_MN_DBHI:
    case ASM_MN_DBLE:
    case ASM_MN_DBLS:
    case ASM_MN_DBLT:
    case ASM_MN_DBMI:
    case ASM_MN_DBNE:
    case ASM_MN_DBPL:
    case ASM_MN_DBVC:
    case ASM_MN_DBVS:
    case ASM_MN_DBRA: return SGRP_DB;
    default: return SGRP_FORBIDDEN;
    }
}

/* Applies the size suffix (text of length sfx_len after the '.', or
 * sfx_len 0 when absent) to the mnemonic's size policy. */
static AsmStatus insn_size(uint8_t mn, const char* sfx, uint8_t sfx_len,
                           uint8_t* out) {
    int group = size_group(mn);
    int have = sfx_len > 0;
    int is_s = 0;
    uint8_t size = ASM_SIZE_NONE;
    if (sfx_len > 1)
        return ASM_ERR_BAD_SIZE;
    if (have) {
        char c = lower(sfx[0]);
        if (c == 'b')
            size = ASM_SIZE_B;
        else if (c == 'w')
            size = ASM_SIZE_W;
        else if (c == 'l')
            size = ASM_SIZE_L;
        else if (c == 's') {
            is_s = 1;
            size = ASM_SIZE_B; /* branch .s: 8-bit displacement */
        } else
            return ASM_ERR_BAD_SIZE;
    }
    switch (group) {
    case SGRP_BWL_REQ:
        if (!have)
            return ASM_ERR_BAD_SIZE;
        if (is_s)
            return ASM_ERR_BAD_SIZE;
        *out = size;
        return ASM_OK;
    case SGRP_BWL_OPT:
        if (is_s)
            return ASM_ERR_BAD_SIZE;
        if (!have)
            *out = ASM_SIZE_W; /* bare shift defaults .w */
        else
            *out = size;
        return ASM_OK;
    case SGRP_WL_REQ:
        if (!have || is_s)
            return ASM_ERR_BAD_SIZE;
        if (size != ASM_SIZE_W && size != ASM_SIZE_L)
            return ASM_ERR_BAD_SIZE;
        *out = size;
        return ASM_OK;
    case SGRP_W_REQ:
        if (!have || size != ASM_SIZE_W)
            return ASM_ERR_BAD_SIZE;
        *out = ASM_SIZE_W;
        return ASM_OK;
    case SGRP_B_OPT:
        if (!have) {
            *out = ASM_SIZE_B;
            return ASM_OK;
        }
        if (is_s || size != ASM_SIZE_B)
            return ASM_ERR_BAD_SIZE;
        *out = ASM_SIZE_B;
        return ASM_OK;
    case SGRP_L_REQ:
        if (!have || size != ASM_SIZE_L)
            return ASM_ERR_BAD_SIZE;
        *out = ASM_SIZE_L;
        return ASM_OK;
    case SGRP_SCC:
        if (!have) {
            *out = ASM_SIZE_B;
            return ASM_OK;
        }
        if (is_s || size != ASM_SIZE_B)
            return ASM_ERR_BAD_SIZE;
        *out = ASM_SIZE_B;
        return ASM_OK;
    case SGRP_BRANCH:
        if (!have) {
            *out = ASM_SIZE_W;
            return ASM_OK;
        }
        if (size == ASM_SIZE_L)
            return ASM_ERR_BAD_SIZE;
        *out = size;
        return ASM_OK;
    case SGRP_DB:
        if (!have) {
            *out = ASM_SIZE_W;
            return ASM_OK;
        }
        if (is_s || size != ASM_SIZE_W)
            return ASM_ERR_BAD_SIZE;
        *out = ASM_SIZE_W;
        return ASM_OK;
    case SGRP_FORBIDDEN:
    default:
        if (have)
            return ASM_ERR_BAD_SIZE;
        *out = ASM_SIZE_NONE;
        return ASM_OK;
    }
}

/* --- Operand parsing ------------------------------------------------ */

typedef struct {
    const AsmToken* t;
    uint8_t count;
    uint8_t pos;
} Tokens;

static const AsmToken* tk(const Tokens* ts, uint8_t ahead) {
    uint16_t i = (uint16_t)(ts->pos + ahead);
    if (i >= ts->count)
        return &ts->t[ts->count]; /* the EOL token */
    return &ts->t[i];
}

/* Collects expression tokens starting at ts->pos into expr. Stops at
 * EOL or COMMA at paren depth 0, at an unbalanced ')', or (when
 * stop_at_mem_tail) at a '(' at depth 0 that begins a memory operand
 * tail (a '(' immediately followed by a register token). When
 * mit_stop is set, also stops at a COMMA at paren depth 1: the
 * separator of the GAS "(d,base[,index])" form. */
static AsmStatus expr_collect(Tokens* ts, AsmExpr* expr, int stop_at_mem_tail,
                              int mit_stop) {
    uint8_t depth = 0;
    expr->count = 0;
    for (;;) {
        const AsmToken* tok = tk(ts, 0);
        uint8_t kind = tok->kind;
        if (kind == ASM_TOK_EOL)
            break;
        if (depth == 0 && kind == ASM_TOK_COMMA)
            break;
        if (mit_stop && depth == 1 && kind == ASM_TOK_COMMA)
            break;
        if (depth == 0 && kind == ASM_TOK_RPAREN)
            break;
        if (depth == 0 && stop_at_mem_tail && kind == ASM_TOK_LPAREN &&
            tk(ts, 1)->kind == ASM_TOK_REG)
            break;
        if (expr->count >= ASM_EXPR_MAX)
            return ASM_ERR_SYNTAX;
        expr->ops[expr->count++] = *tok;
        ts->pos++;
        if (kind == ASM_TOK_LPAREN)
            depth++;
        else if (kind == ASM_TOK_RPAREN && depth > 0)
            depth--;
    }
    return ASM_OK;
}

/* Parses the base [',' index ['.' w/l]] ')' tail of a memory operand.
 * ts->pos is at '(' unless at_base is set (MIT form: the '(' and the
 * displacement's trailing comma are already consumed). have_disp tells
 * whether a displacement expression was collected (postincrement is
 * only valid without one). */
static AsmStatus parse_mem_tail(Tokens* ts, int have_disp, AsmOperand* op,
                                int at_base) {
    const AsmToken* base;
    uint8_t base_reg;
    if (!at_base)
        ts->pos++; /* '(' */
    base = tk(ts, 0);
    if (base->kind != ASM_TOK_REG)
        return ASM_ERR_BAD_OPERAND;
    base_reg = base->reg;
    if (base_reg != ASM_REG_PC &&
        (base_reg < ASM_REG_A0 || base_reg > ASM_REG_A7))
        return ASM_ERR_BAD_OPERAND;
    ts->pos++;
    if (tk(ts, 0)->kind == ASM_TOK_RPAREN) {
        ts->pos++;
        if (tk(ts, 0)->kind == ASM_TOK_PLUS) {
            if (have_disp)
                return ASM_ERR_BAD_OPERAND;
            if (base_reg == ASM_REG_PC)
                return ASM_ERR_BAD_OPERAND;
            ts->pos++; /* consume '+' */
            op->mode = ASM_OP_MEM_POSTINC;
            op->base_reg = base_reg;
            return ASM_OK;
        }
        op->mode = ASM_OP_MEM;
        op->base_reg = base_reg;
        return ASM_OK;
    }
    if (tk(ts, 0)->kind == ASM_TOK_COMMA) {
        const AsmToken* index;
        uint8_t index_reg;
        int index_long = 1;
        ts->pos++;
        index = tk(ts, 0);
        if (index->kind != ASM_TOK_REG)
            return ASM_ERR_BAD_OPERAND;
        index_reg = index->reg;
        if (index_reg > ASM_REG_A7)
            return ASM_ERR_BAD_OPERAND; /* D or A only */
        ts->pos++;
        if (tk(ts, 0)->kind == ASM_TOK_IDENT && tk(ts, 0)->length == 2 &&
            tk(ts, 0)->text[0] == '.') {
            char c = lower(tk(ts, 0)->text[1]);
            if (c == 'w')
                index_long = 0;
            else if (c == 'l')
                index_long = 1;
            else
                return ASM_ERR_BAD_OPERAND;
            ts->pos++;
        }
        if (tk(ts, 0)->kind != ASM_TOK_RPAREN)
            return ASM_ERR_BAD_OPERAND;
        ts->pos++;
        op->mode = ASM_OP_INDEX;
        op->base_reg = base_reg;
        op->index_reg = index_reg;
        op->index_long = (uint8_t)index_long;
        return ASM_OK;
    }
    return ASM_ERR_BAD_OPERAND;
}

/* Parses a movem register list: reg ['-' reg] {'/' reg ['-' reg]}.
 * Ranges must stay within one register file and ascend. Bits: d0=bit0
 * .. d7=bit7, a0=bit8 .. a7=bit15. */
static AsmStatus parse_reglist(Tokens* ts, AsmOperand* op) {
    uint16_t mask = 0;
    for (;;) {
        const AsmToken* r1 = tk(ts, 0);
        uint8_t lo;
        uint8_t hi;
        uint8_t i;
        if (r1->kind != ASM_TOK_REG)
            return ASM_ERR_BAD_OPERAND;
        lo = r1->reg;
        hi = lo;
        ts->pos++;
        if (tk(ts, 0)->kind == ASM_TOK_MINUS) {
            const AsmToken* r2;
            ts->pos++;
            r2 = tk(ts, 0);
            if (r2->kind != ASM_TOK_REG)
                return ASM_ERR_BAD_OPERAND;
            hi = r2->reg;
            ts->pos++;
        }
        if (lo > ASM_REG_A7 || hi > ASM_REG_A7)
            return ASM_ERR_BAD_OPERAND;
        if ((lo >= ASM_REG_A0) != (hi >= ASM_REG_A0))
            return ASM_ERR_BAD_OPERAND;
        if (hi < lo)
            return ASM_ERR_BAD_OPERAND;
        for (i = lo; i <= hi; i++) mask |= (uint16_t)(1u << i);
        if (tk(ts, 0)->kind == ASM_TOK_SLASH) {
            ts->pos++;
            continue;
        }
        break;
    }
    op->mode = ASM_OP_REGLIST;
    op->reg_mask = mask;
    return ASM_OK;
}

static AsmStatus parse_operand(Tokens* ts, AsmOperand* op) {
    const AsmToken* first = tk(ts, 0);
    op->mode = ASM_OP_NONE;
    op->base_reg = ASM_REG_NONE;
    op->index_reg = ASM_REG_NONE;
    op->index_long = 1;
    op->abs_size = 0;
    op->pad = 0;
    op->reg_mask = 0;

    if (first->kind == ASM_TOK_HASH) {
        AsmStatus status;
        ts->pos++;
        status = expr_collect(ts, &op->expr, 0, 0);
        if (status != ASM_OK)
            return status;
        if (op->expr.count == 0)
            return ASM_ERR_SYNTAX;
        op->mode = ASM_OP_IMM;
        return ASM_OK;
    }
    if (first->kind == ASM_TOK_LPAREN && tk(ts, 1)->kind != ASM_TOK_REG) {
        /* GAS MIT form "(d,base[,index])": displacement inside the
         * parentheses. Collect it (expr_collect stops at the comma that
         * precedes the base) and reuse parse_mem_tail. The bare "(base)"
         * form is handled by the generic path below. */
        AsmStatus status = expr_collect(ts, &op->expr, 1, 1);
        if (status != ASM_OK)
            return status;
        if (tk(ts, 0)->kind == ASM_TOK_COMMA) {
            ts->pos++; /* ',' */
            if (op->expr.count > 0) {
                /* balance the still-open outer '(' for the evaluator */
                AsmToken* close;
                if (op->expr.count >= ASM_EXPR_MAX)
                    return ASM_ERR_SYNTAX;
                close = &op->expr.ops[op->expr.count++];
                close->kind = ASM_TOK_RPAREN;
                close->reg = 0;
                close->length = 0;
                close->number = 0;
                close->text = 0;
            }
            return parse_mem_tail(ts, op->expr.count > 0, op, 1);
        }
        if (op->expr.count == 0)
            return ASM_ERR_SYNTAX;
        op->mode = ASM_OP_ABS; /* plain "(expr)" is an absolute expression */
        return ASM_OK;
    }
    if (first->kind == ASM_TOK_REG) {
        if (tk(ts, 1)->kind == ASM_TOK_MINUS ||
            tk(ts, 1)->kind == ASM_TOK_SLASH)
            return parse_reglist(ts, op);
        op->mode = ASM_OP_REG;
        op->base_reg = first->reg;
        ts->pos++;
        return ASM_OK;
    }
    if (first->kind == ASM_TOK_MINUS && tk(ts, 1)->kind == ASM_TOK_LPAREN &&
        tk(ts, 2)->kind == ASM_TOK_REG && tk(ts, 3)->kind == ASM_TOK_RPAREN) {
        const AsmToken* base = tk(ts, 2);
        if (base->reg >= ASM_REG_A0 && base->reg <= ASM_REG_A7) {
            ts->pos += 4;
            op->mode = ASM_OP_MEM_PREDEC;
            op->base_reg = base->reg;
            return ASM_OK;
        }
        /* Not a predecrement form (e.g. -(expr)); fall through to the
         * expression path. */
    }
    {
        AsmStatus status = expr_collect(ts, &op->expr, 1, 0);
        if (status != ASM_OK)
            return status;
        if (tk(ts, 0)->kind == ASM_TOK_LPAREN)
            return parse_mem_tail(ts, op->expr.count > 0, op, 0);
        if (op->expr.count == 0)
            return ASM_ERR_SYNTAX;
        op->mode = ASM_OP_ABS;
        return ASM_OK;
    }
}

/* --- Directives ------------------------------------------------------ */

static AsmStatus expect_eol(const Tokens* ts) {
    if (tk(ts, 0)->kind != ASM_TOK_EOL)
        return ASM_ERR_SYNTAX;
    return ASM_OK;
}

static AsmStatus parse_name_operand(Tokens* ts, AsmStmt* stmt) {
    const AsmToken* tok = tk(ts, 0);
    if (tok->kind != ASM_TOK_IDENT)
        return ASM_ERR_BAD_OPERAND;
    stmt->name = tok->text;
    stmt->name_len = tok->length;
    ts->pos++;
    return expect_eol(ts);
}

/* Resolves the raw bytes of a string token into out (stmt->string_data).
 * Returns the resolved length, or (uint8_t)-1 on a bad escape. */
static uint8_t resolve_string(const char* raw, uint8_t raw_len, char* out) {
    uint8_t n = 0;
    uint8_t i = 0;
    while (i < raw_len) {
        char c = raw[i];
        if (c == '\\') {
            char e;
            char r;
            if (i + 1 >= raw_len)
                return 0xFF;
            e = raw[i + 1];
            if (e == 'n')
                r = '\n';
            else if (e == 't')
                r = '\t';
            else if (e == 'r')
                r = '\r';
            else if (e == '\\')
                r = '\\';
            else if (e == '"')
                r = '"';
            else if (e == '0')
                r = '\0';
            else
                return 0xFF;
            out[n++] = r;
            i += 2;
        } else {
            out[n++] = c;
            i++;
        }
    }
    return n;
}

/* Parses the comma-separated expression list of .byte/.word/.long
 * (.dc.b/.dc.w/.dc.l) into stmt->values. size is ASM_SIZE_B/W/L. */
static AsmStatus parse_data_list(Tokens* ts, AsmStmt* stmt, uint8_t size) {
    uint8_t n = 0;
    stmt->kind = ASM_STMT_DATA;
    stmt->size = size;
    for (;;) {
        AsmStatus status;
        if (n >= ASM_DATA_EXPR_MAX)
            return ASM_ERR_SYNTAX;
        status = expr_collect(ts, &stmt->values[n], 0, 0);
        if (status != ASM_OK)
            return status;
        if (stmt->values[n].count == 0)
            return ASM_ERR_SYNTAX;
        n++;
        if (tk(ts, 0)->kind == ASM_TOK_COMMA) {
            ts->pos++;
            continue;
        }
        break;
    }
    stmt->value_count = n;
    return expect_eol(ts);
}

/* Parses .space/.ds.b/.ds.w/.ds.l. stmt->expr keeps the raw element
 * count; the byte unit (1/2/4) is stored in stmt->ops[0].base_reg
 * (unused for SPACE statements) and the driver multiplies. */
static AsmStatus parse_space(Tokens* ts, AsmStmt* stmt, uint8_t unit) {
    AsmStatus status = expr_collect(ts, &stmt->expr, 0, 0);
    if (status != ASM_OK)
        return status;
    if (stmt->expr.count == 0)
        return ASM_ERR_SYNTAX;
    stmt->kind = ASM_STMT_SPACE;
    stmt->ops[0].base_reg = unit;
    return expect_eol(ts);
}

static AsmStatus parse_directive(Tokens* ts, AsmStmt* stmt) {
    const AsmToken* dir = tk(ts, 0);
    const char* text = dir->text;
    uint8_t len = dir->length;
    ts->pos++;
    if (name_ieq(text, len, ".text")) {
        stmt->kind = ASM_STMT_SECTION;
        stmt->section = ASM_SECTION_TEXT;
        return expect_eol(ts);
    }
    if (name_ieq(text, len, ".data")) {
        stmt->kind = ASM_STMT_SECTION;
        stmt->section = ASM_SECTION_DATA;
        return expect_eol(ts);
    }
    if (name_ieq(text, len, ".bss")) {
        stmt->kind = ASM_STMT_SECTION;
        stmt->section = ASM_SECTION_BSS;
        return expect_eol(ts);
    }
    if (name_ieq(text, len, ".globl") || name_ieq(text, len, ".extern")) {
        AsmStatus status = parse_name_operand(ts, stmt);
        if (status != ASM_OK)
            return status;
        stmt->kind =
            name_ieq(text, len, ".globl") ? ASM_STMT_GLOBAL : ASM_STMT_EXTERN;
        return ASM_OK;
    }
    if (name_ieq(text, len, ".equ")) {
        AsmStatus status;
        const AsmToken* name = tk(ts, 0);
        if (name->kind != ASM_TOK_IDENT)
            return ASM_ERR_BAD_OPERAND;
        stmt->name = name->text;
        stmt->name_len = name->length;
        ts->pos++;
        if (tk(ts, 0)->kind != ASM_TOK_COMMA)
            return ASM_ERR_SYNTAX;
        ts->pos++;
        status = expr_collect(ts, &stmt->expr, 0, 0);
        if (status != ASM_OK)
            return status;
        if (stmt->expr.count == 0)
            return ASM_ERR_SYNTAX;
        stmt->kind = ASM_STMT_EQU;
        return expect_eol(ts);
    }
    if (name_ieq(text, len, ".byte") || name_ieq(text, len, ".dc.b"))
        return parse_data_list(ts, stmt, ASM_SIZE_B);
    if (name_ieq(text, len, ".word") || name_ieq(text, len, ".dc.w"))
        return parse_data_list(ts, stmt, ASM_SIZE_W);
    if (name_ieq(text, len, ".long") || name_ieq(text, len, ".dc.l"))
        return parse_data_list(ts, stmt, ASM_SIZE_L);
    if (name_ieq(text, len, ".space") || name_ieq(text, len, ".ds.b"))
        return parse_space(ts, stmt, 1);
    if (name_ieq(text, len, ".ds.w"))
        return parse_space(ts, stmt, 2);
    if (name_ieq(text, len, ".ds.l"))
        return parse_space(ts, stmt, 4);
    if (name_ieq(text, len, ".even")) {
        stmt->kind = ASM_STMT_EVEN;
        return expect_eol(ts);
    }
    if (name_ieq(text, len, ".end")) {
        stmt->kind = ASM_STMT_END;
        return expect_eol(ts);
    }
    if (name_ieq(text, len, ".ascii") || name_ieq(text, len, ".asciz")) {
        const AsmToken* str = tk(ts, 0);
        uint8_t resolved;
        if (str->kind != ASM_TOK_STRING)
            return ASM_ERR_BAD_OPERAND;
        resolved = resolve_string(str->text, str->length, stmt->string_data);
        if (resolved == 0xFF)
            return ASM_ERR_BAD_OPERAND;
        ts->pos++;
        if (tk(ts, 0)->kind != ASM_TOK_EOL)
            return ASM_ERR_BAD_OPERAND;
        stmt->kind = ASM_STMT_ASCII;
        stmt->string = stmt->string_data;
        stmt->string_len = resolved;
        stmt->string_zero = name_ieq(text, len, ".asciz") ? 1 : 0;
        return ASM_OK;
    }
    return ASM_ERR_UNKNOWN_DIRECTIVE;
}

/* --- Statement parsing ----------------------------------------------- */

static void stmt_zero(AsmStmt* stmt) {
    uint8_t* p = (uint8_t*)stmt;
    uint32_t i;
    for (i = 0; i < sizeof(*stmt); i++) p[i] = 0;
}

AsmStatus asm_parse(const AsmToken* tokens, uint8_t count, AsmStmt* stmt) {
    Tokens ts;
    const AsmToken* head;
    AsmStatus status;
    uint8_t i;
    stmt_zero(stmt);
    for (i = 0; i < 2; i++) {
        stmt->ops[i].base_reg = ASM_REG_NONE;
        stmt->ops[i].index_reg = ASM_REG_NONE;
        stmt->ops[i].index_long = 1;
    }
    ts.t = tokens;
    ts.count = count;
    ts.pos = 0;
    head = tk(&ts, 0);
    if (head->kind == ASM_TOK_EOL) {
        stmt->kind = ASM_STMT_EMPTY;
        return ASM_OK;
    }
    /* Optional label: IDENT ':' or a single-digit NUMBER ':' (numeric
     * label; the label slice keeps the digit character). */
    if (tk(&ts, 1)->kind == ASM_TOK_COLON &&
        ((head->kind == ASM_TOK_IDENT && head->text[0] != '.') ||
         (head->kind == ASM_TOK_NUMBER && head->length == 1 &&
          head->number <= 9))) {
        stmt->label = head->text;
        stmt->label_len = head->length;
        ts.pos += 2;
        head = tk(&ts, 0);
        if (head->kind == ASM_TOK_EOL) {
            stmt->kind = ASM_STMT_EMPTY;
            return ASM_OK;
        }
    }
    if (head->kind != ASM_TOK_IDENT)
        return ASM_ERR_SYNTAX;
    if (head->text[0] == '.')
        return parse_directive(&ts, stmt);
    /* Instruction mnemonic; a trailing '.x' size suffix is part of the
     * identifier token text. */
    {
        const char* text = head->text;
        uint8_t len = head->length;
        uint8_t base_len = len;
        const char* suffix = 0;
        uint8_t suffix_len = 0;
        uint8_t mnemonic = ASM_MN_NONE;
        uint8_t cc = 0;
        uint8_t i;
        for (i = 1; i < len; i++) {
            if (text[i] == '.') {
                base_len = i;
                suffix = text + i + 1;
                suffix_len = (uint8_t)(len - i - 1);
                break;
            }
        }
        if (!asm_mnemonic_lookup(text, base_len, &mnemonic, &cc))
            return ASM_ERR_UNKNOWN_MNEMONIC;
        status = insn_size(mnemonic, suffix, suffix_len, &stmt->size);
        if (status != ASM_OK)
            return status;
        stmt->kind = ASM_STMT_INSN;
        stmt->mnemonic = mnemonic;
        stmt->cc = cc;
        ts.pos++;
        if (tk(&ts, 0)->kind == ASM_TOK_EOL)
            return ASM_OK; /* operand count is the encoder's job */
        status = parse_operand(&ts, &stmt->ops[0]);
        if (status != ASM_OK)
            return status;
        stmt->op_count = 1;
        if (tk(&ts, 0)->kind == ASM_TOK_COMMA) {
            ts.pos++;
            status = parse_operand(&ts, &stmt->ops[1]);
            if (status != ASM_OK)
                return status;
            stmt->op_count = 2;
        }
        return expect_eol(&ts);
    }
}

/* --- Expression evaluation ------------------------------------------- */

typedef struct {
    const AsmCtx* ctx;
    const AsmToken* t;
    uint8_t count;
    uint8_t pos;
    AsmStatus status;
} Eval;

static AsmValue ev_expr(Eval* ev);

/* Kind of the current token, or ASM_TOK_EOL past the last token. The
 * ops array carries no EOL terminator, so peeking must not index it
 * out of range. */
static uint8_t ev_kind(const Eval* ev) {
    if (ev->pos >= ev->count)
        return ASM_TOK_EOL;
    return ev->t[ev->pos].kind;
}

static const AsmToken* ev_tok(const Eval* ev) {
    if (ev->pos >= ev->count)
        return 0;
    return &ev->t[ev->pos];
}

static AsmValue make_abs(int32_t v) {
    AsmValue out;
    out.kind = ASM_VAL_ABS;
    out.section = 0;
    out.value = (uint32_t)v;
    out.symbol = 0;
    return out;
}

static AsmValue make_rel(uint8_t section, uint32_t v) {
    AsmValue out;
    out.kind = ASM_VAL_REL;
    out.section = section;
    out.value = v;
    out.symbol = 0;
    return out;
}

static AsmValue make_extern(uint32_t symbol, int32_t addend) {
    AsmValue out;
    out.kind = ASM_VAL_EXTERN;
    out.section = 0;
    out.value = (uint32_t)addend;
    out.symbol = symbol;
    return out;
}

/* a +/- b with section/extern combination rules. sub selects '-'. */
static AsmValue ev_add(Eval* ev, const AsmValue* a, const AsmValue* b,
                       int sub) {
    if (a->kind == ASM_VAL_ABS && b->kind == ASM_VAL_ABS)
        return make_abs(sub ? (int32_t)a->value - (int32_t)b->value
                            : (int32_t)a->value + (int32_t)b->value);
    if (a->kind == ASM_VAL_REL && b->kind == ASM_VAL_ABS)
        return make_rel(a->section,
                        sub ? a->value - b->value : a->value + b->value);
    if (a->kind == ASM_VAL_ABS && b->kind == ASM_VAL_REL && !sub)
        return make_rel(b->section, a->value + b->value);
    if (a->kind == ASM_VAL_REL && b->kind == ASM_VAL_REL && sub) {
        if (a->section == b->section)
            return make_abs((int32_t)a->value - (int32_t)b->value);
        ev->status = ASM_ERR_NOT_ABSOLUTE;
        return make_abs(0);
    }
    if (a->kind == ASM_VAL_EXTERN && b->kind == ASM_VAL_ABS)
        return make_extern(a->symbol,
                           sub ? (int32_t)a->value - (int32_t)b->value
                               : (int32_t)a->value + (int32_t)b->value);
    ev->status = ASM_ERR_NOT_ABSOLUTE;
    return make_abs(0);
}

/* Multiplication, division, shifts, and bitwise operators: absolute
 * only. Shift amounts are 0..31 with 0 treated as 1 (contract). */
static AsmValue ev_binop(Eval* ev, uint8_t kind, const AsmValue* a,
                         const AsmValue* b) {
    int32_t x;
    int32_t y;
    if (a->kind != ASM_VAL_ABS || b->kind != ASM_VAL_ABS) {
        ev->status = ASM_ERR_NOT_ABSOLUTE;
        return make_abs(0);
    }
    x = (int32_t)a->value;
    y = (int32_t)b->value;
    if (kind == ASM_TOK_STAR)
        return make_abs(x * y);
    if (kind == ASM_TOK_SLASH) {
        if (y == 0) {
            ev->status = ASM_ERR_RANGE;
            return make_abs(0);
        }
        return make_abs(x / y);
    }
    if (kind == ASM_TOK_AND)
        return make_abs(x & y);
    if (kind == ASM_TOK_OR)
        return make_abs(x | y);
    if (kind == ASM_TOK_XOR)
        return make_abs(x ^ y);
    /* shifts */
    if (y < 0 || y > 31) {
        ev->status = ASM_ERR_RANGE;
        return make_abs(0);
    }
    if (y == 0)
        y = 1; /* contract: shift amount 0 is treated as 1 */
    if (kind == ASM_TOK_SHL)
        return make_abs((int32_t)((uint32_t)x << y));
    return make_abs(x >> y);
}

static AsmValue ev_primary(Eval* ev) {
    uint8_t kind = ev_kind(ev);
    const AsmToken* tok = ev_tok(ev);
    if (kind == ASM_TOK_NUMBER) {
        ev->pos++;
        return make_abs((int32_t)tok->number);
    }
    if (kind == ASM_TOK_LPAREN) {
        AsmValue v;
        ev->pos++;
        v = ev_expr(ev);
        if (ev->status != ASM_OK)
            return v;
        if (ev_kind(ev) != ASM_TOK_RPAREN) {
            ev->status = ASM_ERR_SYNTAX;
            return make_abs(0);
        }
        ev->pos++;
        return v;
    }
    if (kind == ASM_TOK_IDENT) {
        const char* name = tok->text;
        uint8_t len = tok->length;
        ev->pos++;
        /* location counter */
        if (len == 1 && name[0] == '.')
            return make_rel(ev->ctx->section,
                            ev->ctx->offset[ev->ctx->section]);
        /* numeric label reference: digit + 'b'/'f' */
        if (len == 2 && name[0] >= '0' && name[0] <= '9' &&
            (lower(name[1]) == 'b' || lower(name[1]) == 'f')) {
            int entry = asm_numeric_lookup(ev->ctx, (uint8_t)(name[0] - '0'),
                                           lower(name[1]) == 'f');
            if (entry < 0) {
                ev->status = ASM_ERR_UNDEFINED_SYMBOL;
                return make_abs(0);
            }
            return make_rel(ev->ctx->numerics[entry].section,
                            ev->ctx->numerics[entry].value);
        }
        {
            int index = asm_sym_lookup(ev->ctx, name, len);
            const AsmSymbol* symbol;
            if (index < 0) {
                ev->status = ASM_ERR_UNDEFINED_SYMBOL;
                return make_abs(0);
            }
            symbol = &ev->ctx->symbols[index];
            if (symbol->section == ASM_SECTION_UNDEF)
                return make_extern((uint32_t)index, 0);
            if (symbol->section == ASM_SECTION_ABS)
                return make_abs((int32_t)symbol->value);
            return make_rel(symbol->section, symbol->value);
        }
    }
    ev->status = ASM_ERR_SYNTAX;
    return make_abs(0);
}

static AsmValue ev_unary(Eval* ev) {
    uint8_t kind = ev_kind(ev);
    if (kind == ASM_TOK_MINUS) {
        AsmValue v;
        ev->pos++;
        v = ev_unary(ev);
        if (ev->status != ASM_OK)
            return v;
        if (v.kind != ASM_VAL_ABS) {
            ev->status = ASM_ERR_NOT_ABSOLUTE;
            return make_abs(0);
        }
        return make_abs(-(int32_t)v.value);
    }
    if (kind == ASM_TOK_NOT) {
        AsmValue v;
        ev->pos++;
        v = ev_unary(ev);
        if (ev->status != ASM_OK)
            return v;
        if (v.kind != ASM_VAL_ABS) {
            ev->status = ASM_ERR_NOT_ABSOLUTE;
            return make_abs(0);
        }
        return make_abs(~(int32_t)v.value);
    }
    return ev_primary(ev);
}

static AsmValue ev_mul(Eval* ev) {
    AsmValue l = ev_unary(ev);
    while (ev->status == ASM_OK &&
           (ev_kind(ev) == ASM_TOK_STAR || ev_kind(ev) == ASM_TOK_SLASH)) {
        uint8_t kind = ev_kind(ev);
        AsmValue r;
        ev->pos++;
        r = ev_unary(ev);
        if (ev->status != ASM_OK)
            return l;
        l = ev_binop(ev, kind, &l, &r);
    }
    return l;
}

static AsmValue ev_add_level(Eval* ev) {
    AsmValue l = ev_mul(ev);
    while (ev->status == ASM_OK &&
           (ev_kind(ev) == ASM_TOK_PLUS || ev_kind(ev) == ASM_TOK_MINUS)) {
        int sub = ev_kind(ev) == ASM_TOK_MINUS;
        AsmValue r;
        ev->pos++;
        r = ev_mul(ev);
        if (ev->status != ASM_OK)
            return l;
        l = ev_add(ev, &l, &r, sub);
    }
    return l;
}

static AsmValue ev_shift(Eval* ev) {
    AsmValue l = ev_add_level(ev);
    while (ev->status == ASM_OK &&
           (ev_kind(ev) == ASM_TOK_SHL || ev_kind(ev) == ASM_TOK_SHR)) {
        uint8_t kind = ev_kind(ev);
        AsmValue r;
        ev->pos++;
        r = ev_add_level(ev);
        if (ev->status != ASM_OK)
            return l;
        l = ev_binop(ev, kind, &l, &r);
    }
    return l;
}

static AsmValue ev_and(Eval* ev) {
    AsmValue l = ev_shift(ev);
    while (ev->status == ASM_OK && ev_kind(ev) == ASM_TOK_AND) {
        AsmValue r;
        ev->pos++;
        r = ev_shift(ev);
        if (ev->status != ASM_OK)
            return l;
        l = ev_binop(ev, ASM_TOK_AND, &l, &r);
    }
    return l;
}

static AsmValue ev_xor(Eval* ev) {
    AsmValue l = ev_and(ev);
    while (ev->status == ASM_OK && ev_kind(ev) == ASM_TOK_XOR) {
        AsmValue r;
        ev->pos++;
        r = ev_and(ev);
        if (ev->status != ASM_OK)
            return l;
        l = ev_binop(ev, ASM_TOK_XOR, &l, &r);
    }
    return l;
}

static AsmValue ev_or(Eval* ev) {
    AsmValue l = ev_xor(ev);
    while (ev->status == ASM_OK && ev_kind(ev) == ASM_TOK_OR) {
        AsmValue r;
        ev->pos++;
        r = ev_xor(ev);
        if (ev->status != ASM_OK)
            return l;
        l = ev_binop(ev, ASM_TOK_OR, &l, &r);
    }
    return l;
}

static AsmValue ev_expr(Eval* ev) { return ev_or(ev); }

AsmStatus asm_expr_eval(const AsmCtx* ctx, const AsmExpr* expr, AsmValue* out) {
    Eval ev;
    AsmValue v;
    if (expr->count == 0) {
        /* Empty expression: displacement 0 for bare (An)/(PC) forms. */
        *out = make_abs(0);
        return ASM_OK;
    }
    ev.ctx = ctx;
    ev.t = expr->ops;
    ev.count = expr->count;
    ev.pos = 0;
    ev.status = ASM_OK;
    v = ev_or(&ev);
    if (ev.status != ASM_OK)
        return ev.status;
    if (ev.pos != expr->count)
        return ASM_ERR_SYNTAX;
    *out = v;
    return ASM_OK;
}
