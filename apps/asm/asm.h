#ifndef SSOS9_APPS_ASM_ASM_H
#define SSOS9_APPS_ASM_ASM_H

/* Shared interfaces for the SSOS-9 MC68000 assembler. See
 * docs/assembler-contract.md for the syntax, object format, and
 * relocation contracts these types implement. */

#include <stdint.h>

#define ASM_LINE_MAX 200
#define ASM_LINE_COUNT_MAX 1200
#define ASM_TEXT_MAX 24576u
#define ASM_DATA_MAX 6144u
#define ASM_BSS_MAX 65536u
#define ASM_SYMBOL_MAX 256
#define ASM_RELOC_MAX 512
#define ASM_NUMERIC_MAX 64
#define ASM_NAME_MAX 31
#define ASM_EXPR_MAX 16
#define ASM_DATA_EXPR_MAX 8
#define ASM_TOKEN_MAX 96
#define ASM_OBJ_HEADER_SIZE 40
#define ASM_OBJ_SYMBOL_SIZE 12
#define ASM_OBJ_RELOC_SIZE 16
#define ASM_GLOBAL_MAX 64

#define ASM_SECTION_ABS 0
#define ASM_SECTION_TEXT 1
#define ASM_SECTION_DATA 2
#define ASM_SECTION_BSS 3
#define ASM_SECTION_UNDEF 0xFF

#define ASM_RELOC_PC16 1
#define ASM_RELOC_ABS32 2
#define ASM_RELOC_ABS16 3
#define ASM_RELOC_ABS8 4

#define ASM_SYM_GLOBAL 0x01u
#define ASM_SYM_EXTERN 0x02u
#define ASM_SYM_SECTION 0x04u /* reserved section symbol (name "\001text" etc.) */

typedef enum {
    ASM_OK = 0,
    ASM_ERR_SYNTAX,
    ASM_ERR_LINE_LONG,
    ASM_ERR_UNKNOWN_MNEMONIC,
    ASM_ERR_UNKNOWN_DIRECTIVE,
    ASM_ERR_UNKNOWN_REGISTER,
    ASM_ERR_BAD_SIZE,
    ASM_ERR_BAD_OPERAND,
    ASM_ERR_OPERAND_COUNT,
    ASM_ERR_UNDEFINED_SYMBOL,
    ASM_ERR_DUPLICATE_SYMBOL,
    ASM_ERR_RANGE,
    ASM_ERR_TEXT_FULL,
    ASM_ERR_DATA_FULL,
    ASM_ERR_BSS_FULL,
    ASM_ERR_SYMBOL_TABLE_FULL,
    ASM_ERR_RELOC_TABLE_FULL,
    ASM_ERR_NAME_LONG,
    ASM_ERR_NOT_ABSOLUTE,
    ASM_ERR_WRONG_SECTION,
    ASM_ERR_UNSUPPORTED,
    ASM_ERR_WRITE,
    ASM_ERR_SOURCE_LONG
} AsmStatus;

enum {
    ASM_REG_D0 = 0,
    ASM_REG_D1,
    ASM_REG_D2,
    ASM_REG_D3,
    ASM_REG_D4,
    ASM_REG_D5,
    ASM_REG_D6,
    ASM_REG_D7,
    ASM_REG_A0 = 8,
    ASM_REG_A1,
    ASM_REG_A2,
    ASM_REG_A3,
    ASM_REG_A4,
    ASM_REG_A5,
    ASM_REG_A6,
    ASM_REG_A7,
    ASM_REG_PC = 16,
    ASM_REG_CCR = 17,
    ASM_REG_SR = 18,
    ASM_REG_USP = 19,
    ASM_REG_NONE = 0xFF
};

/* Condition codes shared by bcc/dbcc/scc (0=T .. 15=LE). */
#define ASM_CC_T 0
#define ASM_CC_F 1
#define ASM_CC_HI 2
#define ASM_CC_LS 3
#define ASM_CC_CC 4
#define ASM_CC_CS 5
#define ASM_CC_NE 6
#define ASM_CC_EQ 7
#define ASM_CC_VC 8
#define ASM_CC_VS 9
#define ASM_CC_PL 10
#define ASM_CC_MI 11
#define ASM_CC_GE 12
#define ASM_CC_LT 13
#define ASM_CC_GT 14
#define ASM_CC_LE 15

#define ASM_SIZE_NONE 0
#define ASM_SIZE_B 1
#define ASM_SIZE_W 2
#define ASM_SIZE_L 3

typedef enum {
    ASM_MN_NONE = 0,
    ASM_MN_ABCD,
    ASM_MN_ADD,
    ASM_MN_ADDA,
    ASM_MN_ADDI,
    ASM_MN_ADDQ,
    ASM_MN_AND,
    ASM_MN_ANDI,
    ASM_MN_ASL,
    ASM_MN_ASR,
    ASM_MN_BCC,
    ASM_MN_BCS,
    ASM_MN_BEQ,
    ASM_MN_BGE,
    ASM_MN_BGT,
    ASM_MN_BHI,
    ASM_MN_BLE,
    ASM_MN_BLS,
    ASM_MN_BLT,
    ASM_MN_BMI,
    ASM_MN_BNE,
    ASM_MN_BPL,
    ASM_MN_BVC,
    ASM_MN_BVS,
    ASM_MN_BRA,
    ASM_MN_BSR,
    ASM_MN_BCHG,
    ASM_MN_BCLR,
    ASM_MN_BSET,
    ASM_MN_BTST,
    ASM_MN_CHK,
    ASM_MN_CLR,
    ASM_MN_CMP,
    ASM_MN_CMPA,
    ASM_MN_CMPI,
    ASM_MN_CMPM,
    ASM_MN_DBCC,
    ASM_MN_DBCS,
    ASM_MN_DBEQ,
    ASM_MN_DBGE,
    ASM_MN_DBGT,
    ASM_MN_DBHI,
    ASM_MN_DBLE,
    ASM_MN_DBLS,
    ASM_MN_DBLT,
    ASM_MN_DBMI,
    ASM_MN_DBNE,
    ASM_MN_DBPL,
    ASM_MN_DBVC,
    ASM_MN_DBVS,
    ASM_MN_DBRA,
    ASM_MN_DIVS,
    ASM_MN_DIVU,
    ASM_MN_EOR,
    ASM_MN_EORI,
    ASM_MN_EXG,
    ASM_MN_EXT,
    ASM_MN_ILLEGAL,
    ASM_MN_JMP,
    ASM_MN_JSR,
    ASM_MN_LEA,
    ASM_MN_LINK,
    ASM_MN_LSL,
    ASM_MN_LSR,
    ASM_MN_MOVE,
    ASM_MN_MOVEA,
    ASM_MN_MOVEM,
    ASM_MN_MOVEQ,
    ASM_MN_MOVEP,
    ASM_MN_MULS,
    ASM_MN_MULU,
    ASM_MN_NBCD,
    ASM_MN_NEG,
    ASM_MN_NEGX,
    ASM_MN_NOP,
    ASM_MN_NOT,
    ASM_MN_OR,
    ASM_MN_ORI,
    ASM_MN_PEA,
    ASM_MN_RESET,
    ASM_MN_ROL,
    ASM_MN_ROR,
    ASM_MN_ROXL,
    ASM_MN_ROXR,
    ASM_MN_RTR,
    ASM_MN_RTE,
    ASM_MN_RTS,
    ASM_MN_SBCD,
    ASM_MN_SCC,
    ASM_MN_SCS,
    ASM_MN_SEQ,
    ASM_MN_SGE,
    ASM_MN_SGT,
    ASM_MN_SHI,
    ASM_MN_SLE,
    ASM_MN_SLS,
    ASM_MN_SLT,
    ASM_MN_SMI,
    ASM_MN_SNE,
    ASM_MN_SPL,
    ASM_MN_SVC,
    ASM_MN_SVS,
    ASM_MN_STOP,
    ASM_MN_SUB,
    ASM_MN_SUBA,
    ASM_MN_SUBI,
    ASM_MN_SUBQ,
    ASM_MN_SWAP,
    ASM_MN_TAS,
    ASM_MN_TRAP,
    ASM_MN_TRAPV,
    ASM_MN_TST,
    ASM_MN_UNLINK
} AsmMnemonic;

typedef enum {
    ASM_TOK_EOL = 0,
    ASM_TOK_IDENT,
    ASM_TOK_NUMBER,
    ASM_TOK_STRING,
    ASM_TOK_REG,
    ASM_TOK_HASH,
    ASM_TOK_LPAREN,
    ASM_TOK_RPAREN,
    ASM_TOK_COMMA,
    ASM_TOK_COLON,
    ASM_TOK_PLUS,
    ASM_TOK_MINUS,
    ASM_TOK_STAR,
    ASM_TOK_SLASH,
    ASM_TOK_AND,
    ASM_TOK_OR,
    ASM_TOK_XOR,
    ASM_TOK_NOT,
    ASM_TOK_SHL,
    ASM_TOK_SHR
} AsmTokenKind;

typedef struct {
    uint8_t kind;         /* AsmTokenKind */
    uint8_t reg;          /* ASM_TOK_REG: ASM_REG_* */
    uint8_t length;       /* ident/string text length */
    uint32_t number;      /* ASM_TOK_NUMBER */
    const char* text;     /* ident/string slice into the source line */
} AsmToken;

typedef struct {
    uint8_t count;
    AsmToken ops[ASM_EXPR_MAX];
} AsmExpr;

typedef enum {
    ASM_OP_NONE = 0,
    ASM_OP_REG,      /* reg field: D/A/CCR/SR/USP register direct */
    ASM_OP_IMM,      /* expr = immediate */
    ASM_OP_ABS,      /* expr = address; abs_size: 0 default(.l), W, L */
    ASM_OP_MEM,      /* base_reg + expr displacement (d16(An), d16(PC)) */
    ASM_OP_MEM_POSTINC,
    ASM_OP_MEM_PREDEC,
    ASM_OP_INDEX,    /* base_reg + expr(d8) + index_reg/index_long */
    ASM_OP_REGLIST   /* reg_mask */
} AsmOperandMode;

typedef struct {
    uint8_t mode;       /* AsmOperandMode */
    uint8_t base_reg;   /* MEM/INDEX: ASM_REG_* */
    uint8_t index_reg;  /* INDEX: ASM_REG_D0..A7 */
    uint8_t index_long; /* INDEX: 1 = .l (default), 0 = .w */
    uint8_t abs_size;   /* ABS: 0 default, ASM_SIZE_W, ASM_SIZE_L */
    uint8_t pad;
    uint16_t reg_mask;  /* REGLIST */
    AsmExpr expr;       /* IMM/ABS/MEM/INDEX expression */
} AsmOperand;

typedef enum {
    ASM_STMT_EMPTY = 0, /* label or comment only */
    ASM_STMT_INSN,
    ASM_STMT_DATA,      /* values/value_count */
    ASM_STMT_ASCII,     /* string/string_len/string_zero */
    ASM_STMT_SPACE,     /* expr = byte count */
    ASM_STMT_EQU,       /* name/name_len + expr */
    ASM_STMT_SECTION,   /* section */
    ASM_STMT_GLOBAL,    /* name/name_len */
    ASM_STMT_EXTERN,    /* name/name_len */
    ASM_STMT_EVEN,
    ASM_STMT_END
} AsmStmtKind;

typedef struct {
    uint8_t kind;         /* AsmStmtKind */
    const char* label;    /* optional label slice */
    uint8_t label_len;
    uint8_t mnemonic;     /* AsmMnemonic, ASM_STMT_INSN */
    uint8_t cc;           /* condition code for cc-form instructions */
    uint8_t size;         /* ASM_SIZE_* */
    uint8_t op_count;
    AsmOperand ops[2];
    AsmExpr values[ASM_DATA_EXPR_MAX]; /* ASM_STMT_DATA */
    uint8_t value_count;
    const char* string;   /* ASM_STMT_ASCII resolved bytes (string_data) */
    uint8_t string_len;
    uint8_t string_zero;  /* ASM_STMT_ASCII: 1 = NUL terminated */
    char string_data[ASM_LINE_MAX]; /* string storage (modules have no .bss) */
    AsmExpr expr;         /* SPACE/EQU value */
    uint8_t section;      /* ASM_STMT_SECTION target */
    const char* name;     /* EQU/GLOBAL/EXTERN symbol slice */
    uint8_t name_len;
} AsmStmt;

typedef struct {
    char name[ASM_NAME_MAX + 1];
    uint8_t section;   /* ASM_SECTION_* or UNDEF */
    uint8_t flags;     /* ASM_SYM_* */
    uint16_t def_pass; /* pass that defined the label; 0 = none/extern */
    uint32_t value;
} AsmSymbol;

typedef struct {
    uint8_t section; /* section the offset is measured in */
    uint8_t type;    /* ASM_RELOC_* */
    uint16_t pad;
    uint32_t offset;
    uint32_t symbol; /* index into AsmCtx.symbols */
    int32_t addend;
} AsmReloc;

typedef enum {
    ASM_VAL_ABS,    /* value is absolute */
    ASM_VAL_REL,    /* section + value: section-relative */
    ASM_VAL_EXTERN  /* symbol index + value addend */
} AsmValKind;

typedef struct {
    uint8_t kind;    /* AsmValKind */
    uint8_t section; /* REL only */
    uint32_t value;  /* ABS: value, REL: section offset, EXTERN: addend */
    uint32_t symbol; /* EXTERN only: symbol index */
} AsmValue;

typedef struct {
    uint32_t pass;      /* 1 or 2 */
    uint32_t line;      /* current source line, 1-based */
    uint32_t line_count;
    uint8_t section;    /* current output section */
    uint8_t ended;      /* .end seen */
    uint16_t pad;
    uint32_t offset[4]; /* current offset per section, [0] unused */
    uint8_t text[ASM_TEXT_MAX];
    uint8_t data[ASM_DATA_MAX];
    uint32_t bss_size;
    AsmSymbol symbols[ASM_SYMBOL_MAX];
    uint32_t symbol_count;
    AsmReloc relocs[ASM_RELOC_MAX];
    uint32_t reloc_count;
    struct {
        uint8_t digit;
        uint8_t section;
        uint32_t line;
        uint32_t value; /* section-relative offset of the definition */
    } numerics[ASM_NUMERIC_MAX];
    uint32_t numeric_count;
    struct {
        char name[ASM_NAME_MAX + 1];
        uint8_t length;
    } globals[ASM_GLOBAL_MAX];
    uint32_t global_count;
    AsmStatus status;
    uint32_t error_line;
} AsmCtx;

/* --- ctx.c: context, symbols, emission, relocations --- */

void asm_ctx_init(AsmCtx* ctx);
const char* asm_status_message(AsmStatus status);
AsmStatus asm_ctx_error(AsmCtx* ctx, AsmStatus status); /* records line, returns status */

int asm_sym_lookup(const AsmCtx* ctx, const char* name, uint8_t len); /* index or -1 */
AsmStatus asm_sym_define(AsmCtx* ctx, const char* name, uint8_t len, uint8_t section, uint32_t value);
AsmStatus asm_sym_extern(AsmCtx* ctx, const char* name, uint8_t len);
AsmStatus asm_sym_global(AsmCtx* ctx, const char* name, uint8_t len);
/* Records a numeric label definition at the current line, section, and
 * section offset. */
AsmStatus asm_numeric_define(AsmCtx* ctx, uint8_t digit);
/* Entry index in ctx->numerics for the backward/forward match of the current
 * line, or -1. */
int asm_numeric_lookup(const AsmCtx* ctx, uint8_t digit, int forward);

/* Emission appends to the current section (pass 2 only; pass 1 callers
 * advance ctx->offset themselves). */
AsmStatus asm_emit8(AsmCtx* ctx, uint32_t value);
AsmStatus asm_emit16(AsmCtx* ctx, uint32_t value);
AsmStatus asm_emit32(AsmCtx* ctx, uint32_t value);
AsmStatus asm_emit_bytes(AsmCtx* ctx, const uint8_t* bytes, uint32_t count);
AsmStatus asm_align_even(AsmCtx* ctx); /* pads the current section to even */

AsmStatus asm_reloc_add(AsmCtx* ctx, uint8_t section, uint8_t type, uint32_t offset,
                        uint32_t symbol_index, int32_t addend);

/* Returns the index of the reserved section symbol for a section, creating
 * it on demand (flags ASM_SYM_SECTION, value 0). -1 when the table is full.
 * Used to express relocations whose target is section-relative. */
int asm_section_symbol(AsmCtx* ctx, uint8_t section);

/* --- lex.c --- */

/* Tokenizes one NUL-terminated source line (without its newline). Writes at
 * most max tokens and always terminates with ASM_TOK_EOL. */
AsmStatus asm_lex(const char* line, AsmToken* tokens, uint8_t max, uint8_t* count);

/* --- parse.c --- */

/* Maps a mnemonic identifier (without size suffix) to its enum. cc receives
 * the condition code for cc-form instructions. Returns 1 when found. */
int asm_mnemonic_lookup(const char* name, uint8_t len, uint8_t* mnemonic, uint8_t* cc);

/* Parses one tokenized line into stmt. Tokens may be overwritten. */
AsmStatus asm_parse(const AsmToken* tokens, uint8_t count, AsmStmt* stmt);

/* Evaluates an expression against the context symbols and numeric labels.
 * Numeric labels resolve relative to ctx->line (pass 2 only; pass 1 callers
 * never evaluate forward references). */
AsmStatus asm_expr_eval(const AsmCtx* ctx, const AsmExpr* expr, AsmValue* out);

/* --- encode.c --- */

/* Instruction size in bytes for pass 1. stmt must be ASM_STMT_INSN. */
AsmStatus asm_insn_size(const AsmStmt* stmt, uint32_t* size);

/* Encodes one instruction into ctx (pass 2). */
AsmStatus asm_encode_insn(AsmCtx* ctx, const AsmStmt* stmt);

/* --- obj.c --- */

typedef int (*AsmObjSink)(const void* bytes, uint32_t length, void* user);

/* Serializes the S9O1 object incrementally through sink. The sink returns 0
 * on success, non-zero on failure (reported as ASM_ERR_WRITE). */
AsmStatus asm_obj_write(const AsmCtx* ctx, AsmObjSink sink, void* user);

/* --- assemble.c --- */

/* Starts a pass: sets ctx->pass and resets the line counter. Call once per
 * pass before feeding lines. */
void asm_assemble_begin(AsmCtx* ctx, uint32_t pass);

/* Processes one source line (without newline): lexes, parses, and executes
 * the statement for the current pass. Returns the first error (recorded in
 * ctx). ctx->ended is set when .end is seen; callers stop feeding lines. */
AsmStatus asm_assemble_line(AsmCtx* ctx, const char* line);

/* Finishes a pass. Pass 2 resolves deferred .globl declarations and reports
 * still-undefined symbols as errors. */
AsmStatus asm_assemble_end(AsmCtx* ctx);

/* Derives the default output name: the source name with its extension
 * replaced by ".O" (contract CLI). The source base must fit the FAT16
 * 8.3 rule; ASM_ERR_NAME_LONG otherwise. out holds 16 bytes. */
AsmStatus asm_default_output(const char* source, char* out);

#endif
