/* Instruction sizing and MC68000 machine-code encoding.
 * See docs/assembler-contract.md for the instruction, encoding, and
 * relocation contracts implemented here.
 *
 * asm_insn_size() runs the same encoder as a dry pass (no emission, no
 * expression evaluation), so sizes and encodings agree by construction.
 * Sizes depend only on the parsed form, never on symbol values; value
 * dependent checks (ranges, relocations) run only in the encode pass. */

#include "asm.h"

#include <stddef.h> /* NULL (freestanding) */

/* Provided by ctx.c: reserved section anchor symbol ("\001text" etc.,
 * flags ASM_SYM_SECTION, value 0) for relocations whose target expression
 * evaluates to ASM_VAL_REL. Returns the symbol index or -1 when the table
 * is full. */
int asm_section_symbol(AsmCtx* ctx, uint8_t section);

/* --- structural effective-address classification --- */

enum {
    K_DN = 1u << 0,    /* %dn direct */
    K_AN = 1u << 1,    /* %an direct */
    K_IND = 1u << 2,   /* (%an) */
    K_POST = 1u << 3,  /* (%an)+ */
    K_PRE = 1u << 4,   /* -(%an) */
    K_D16AN = 1u << 5, /* d16(%an) */
    K_IXAN = 1u << 6,  /* d8(%an,%xn) */
    K_ABSW = 1u << 7,  /* expr.w */
    K_ABSL = 1u << 8,  /* expr / expr.l */
    K_D16PC = 1u << 9, /* d16(%pc), (%pc) */
    K_IXPC = 1u << 10, /* d8(%pc,%xn) */
    K_IMM = 1u << 11   /* #expr */
};

/* Common mode-class masks (real MC68000 semantics, verified against
 * m68k-xelf-as -m68000). "Data" excludes An direct; "alterable" excludes
 * PC-relative and immediate. */
#define G_DATA                                                                 \
    (K_DN | K_IND | K_POST | K_PRE | K_D16AN | K_IXAN | K_ABSW | K_ABSL |      \
     K_D16PC | K_IXPC | K_IMM)
#define G_DATA_AN (G_DATA | K_AN) /* data + An direct (w/.l) */
#define G_ALTER                                                                \
    (K_DN | K_AN | K_IND | K_POST | K_PRE | K_D16AN | K_IXAN | K_ABSW | K_ABSL)
#define G_DALT (G_ALTER & ~(uint32_t)K_AN) /* data alterable */
#define G_MEMD (G_DALT & ~(uint32_t)K_DN)  /* alterable memory */
#define G_CTRL (K_IND | K_D16AN | K_IXAN | K_ABSW | K_ABSL | K_D16PC | K_IXPC)

/* MOVE size field encoding: .b=01, .w=11, .l=10. */
static uint32_t move_size_code(uint8_t size) {
    if (size == ASM_SIZE_B)
        return 1u;
    if (size == ASM_SIZE_W)
        return 3u;
    return 2u;
}

/* Generic 68000 size field (bits 7-6): 00=.b 01=.w 10=.l. Note the
 * ASM_SIZE_B/W/L enum values are 1/2/3 and must never be shifted into
 * an opcode directly. Bare (size-less) forms encode 00 as well; their
 * opmode bits live in the base opcode (verified against GAS: nbcd
 * encodes 48xx, tas 4ACx). */
#define SIZE_BITS(size)                                                        \
    ((size) == ASM_SIZE_W ? 0x40u : (size) == ASM_SIZE_L ? 0x80u : 0u)

typedef struct {
    AsmCtx* ctx; /* NULL: dry run (size only) */
    const AsmStmt* stmt;
    uint32_t start; /* section offset of the instruction start */
    uint32_t pos;   /* bytes covered so far (opcode + extensions) */
} Enc;

static AsmStatus err(Enc* e, AsmStatus status) {
    if (e->ctx != NULL)
        return asm_ctx_error(e->ctx, status);
    return status;
}

static AsmStatus em16(Enc* e, uint32_t word) {
    e->pos += 2;
    if (e->ctx == NULL)
        return ASM_OK;
    return asm_emit16(e->ctx, word);
}

static AsmStatus em32(Enc* e, uint32_t words) {
    e->pos += 4;
    if (e->ctx == NULL)
        return ASM_OK;
    return asm_emit32(e->ctx, words);
}

/* Expression evaluation wrapper: the dry pass never evaluates (sizes are
 * value independent), it only accounts for the field width. */
static AsmStatus ev(Enc* e, const AsmExpr* expr, AsmValue* out) {
    if (e->ctx == NULL) {
        out->kind = ASM_VAL_ABS;
        out->section = ASM_SECTION_ABS;
        out->value = 0;
        out->symbol = 0;
        return ASM_OK;
    }
    return asm_expr_eval(e->ctx, expr, out);
}

static int fit_d8(uint32_t v) {
    return (int32_t)v >= -128 && (int32_t)v <= 127;
}

static int fit_d16(uint32_t v) {
    return (int32_t)v >= -32768 && (int32_t)v <= 32767;
}

static int fit_w(uint32_t v) { /* word fields accepting 0..65535 */
    return (int32_t)v >= -32768 && (int32_t)v <= 65535;
}

static int fit_b(uint32_t v) { /* byte fields accepting 0..255 */
    return (int32_t)v >= -128 && (int32_t)v <= 255;
}

static uint32_t field_off(const Enc* e) { return e->start + e->pos; }

static AsmStatus add_reloc(Enc* e, uint8_t type, uint32_t off, uint32_t sym,
                           int32_t addend) {
    return asm_reloc_add(e->ctx, e->ctx->section, type, off, sym, addend);
}

/* Absolute (non PC-relative) 16/32-bit data field: displacement of
 * d16(An), absolute short/long addresses. REL targets relocate against
 * their section anchor symbol and keep the section-relative value in the
 * field bytes; externals emit 0 plus the constant addend. dsp selects the
 * 16-bit value range: displacements are signed (-32768..32767, verified
 * against GAS rejecting 32768(%an)), absolute .w addresses 0..65535. */
static AsmStatus field_abs_dsp(Enc* e, const AsmExpr* expr, uint8_t type,
                               int dsp) {
    AsmValue v;
    AsmStatus st = ev(e, expr, &v);
    uint32_t off = field_off(e);
    int (*fits)(uint32_t) = dsp ? fit_d16 : fit_w;
    if (st != ASM_OK)
        return err(e, st);
    if (e->ctx == NULL) {
        e->pos += (type == ASM_RELOC_ABS32) ? 4u : 2u;
        return ASM_OK;
    }
    if (v.kind == ASM_VAL_ABS) {
        if (type == ASM_RELOC_ABS32)
            return em32(e, v.value);
        if (!fits(v.value))
            return err(e, ASM_ERR_RANGE);
        return em16(e, v.value & 0xFFFFu);
    }
    if (v.kind == ASM_VAL_EXTERN) {
        st = (type == ASM_RELOC_ABS32) ? em32(e, 0) : em16(e, 0);
        if (st != ASM_OK)
            return st;
        return add_reloc(e, type, off, v.symbol, (int32_t)v.value);
    }
    {
        int sym = asm_section_symbol(e->ctx, v.section);
        if (sym < 0)
            return err(e, ASM_ERR_SYMBOL_TABLE_FULL);
        if (type == ASM_RELOC_ABS32) {
            st = em32(e, v.value);
        } else {
            if (!fits(v.value))
                return err(e, ASM_ERR_RANGE);
            st = em16(e, v.value & 0xFFFFu);
        }
        if (st != ASM_OK)
            return st;
        return add_reloc(e, type, off, (uint32_t)sym, (int32_t)v.value);
    }
}

static AsmStatus field_abs(Enc* e, const AsmExpr* expr, uint8_t type) {
    return field_abs_dsp(e, expr, type, 0);
}

/* 16-bit PC-relative field: branch .w extension, dbcc displacement,
 * d16(%pc). Same-section REL targets resolve at assembly time with no
 * relocation; the displacement counts from the field itself (the program
 * counter points at the extension word). Cross-section REL and externals
 * emit a PC16 relocation at the field offset. */
static AsmStatus field_pc16(Enc* e, const AsmExpr* expr) {
    AsmValue v;
    AsmStatus st = ev(e, expr, &v);
    uint32_t off = field_off(e);
    if (st != ASM_OK)
        return err(e, st);
    if (e->ctx == NULL) {
        e->pos += 2;
        return ASM_OK;
    }
    if (v.kind == ASM_VAL_REL && v.section == e->ctx->section) {
        int32_t disp = (int32_t)v.value - (int32_t)off;
        if (!fit_d16((uint32_t)disp))
            return err(e, ASM_ERR_RANGE);
        return em16(e, (uint32_t)disp & 0xFFFFu);
    }
    if (v.kind == ASM_VAL_ABS) {
        /* Absolute constants are literal displacements (GAS parity:
         * move.w 4(%pc),%d0 keeps 0004, no target subtraction). */
        if (!fit_d16(v.value))
            return err(e, ASM_ERR_RANGE);
        return em16(e, v.value & 0xFFFFu);
    }
    if (v.kind == ASM_VAL_EXTERN) {
        st = em16(e, 0);
        if (st != ASM_OK)
            return st;
        return add_reloc(e, ASM_RELOC_PC16, off, v.symbol, (int32_t)v.value);
    }
    {
        int sym = asm_section_symbol(e->ctx, v.section);
        if (sym < 0)
            return err(e, ASM_ERR_SYMBOL_TABLE_FULL);
        st = em16(e, v.value & 0xFFFFu);
        if (st != ASM_OK)
            return st;
        return add_reloc(e, ASM_RELOC_PC16, off, (uint32_t)sym,
                         (int32_t)v.value);
    }
}

/* Brief index extension word base: D/A (bit 15), register number
 * (bits 14-12), W/L (bit 11, 1 = .l). */
static uint32_t ix_base(const AsmOperand* op) {
    return ((op->index_reg & 0x08u) ? 0x8000u : 0u) |
           ((uint32_t)(op->index_reg & 7u) << 12) |
           ((op->index_long ? 1u : 0u) << 11);
}

/* Brief index extension word for d8(An,Xn): absolute displacement. */
static AsmStatus field_ix_an(Enc* e, const AsmOperand* op) {
    AsmValue v;
    AsmStatus st = ev(e, &op->expr, &v);
    if (st != ASM_OK)
        return err(e, st);
    if (e->ctx == NULL) {
        e->pos += 2;
        return ASM_OK;
    }
    if (v.kind != ASM_VAL_ABS)
        return err(e, ASM_ERR_NOT_ABSOLUTE);
    if (!fit_d8(v.value))
        return err(e, ASM_ERR_RANGE);
    return em16(e, ix_base(op) | (v.value & 0xFFu));
}

/* Brief index extension word for d8(%pc,Xn): PC-relative d8. Same-section
 * REL resolves from the extension word position; externals relocate with
 * PC16 and clear the displacement byte. */
static AsmStatus field_ix_pc(Enc* e, const AsmOperand* op) {
    AsmValue v;
    AsmStatus st = ev(e, &op->expr, &v);
    uint32_t off = field_off(e);
    uint32_t base = ix_base(op);
    if (st != ASM_OK)
        return err(e, st);
    if (e->ctx == NULL) {
        e->pos += 2;
        return ASM_OK;
    }
    if (v.kind == ASM_VAL_REL && v.section == e->ctx->section) {
        int32_t disp = (int32_t)v.value - (int32_t)off;
        if (!fit_d8((uint32_t)disp))
            return err(e, ASM_ERR_RANGE);
        return em16(e, base | ((uint32_t)disp & 0xFFu));
    }
    if (v.kind == ASM_VAL_ABS) {
        /* Literal displacement byte, GAS parity: 4(%pc,%d3.l) keeps 04. */
        if (!fit_d8(v.value))
            return err(e, ASM_ERR_RANGE);
        return em16(e, base | (v.value & 0xFFu));
    }
    /* Symbol displacements end here: S9O1 has no PC8 relocation, and
     * patching the brief extension word's displacement byte with PC16
     * would corrupt the D/A, register, and W/L bits (GAS emits R_68K_PC8
     * with the addend 1 at the value byte for this form). Same rule as
     * d8(%an,%xn), which field_ix_an rejects as NOT_ABSOLUTE. */
    return err(e, ASM_ERR_NOT_ABSOLUTE);
}

/* Immediate data field sized by the instruction size (.b occupies one
 * word with the value in the low byte). */
static AsmStatus field_imm(Enc* e, const AsmOperand* op, uint8_t size) {
    AsmValue v;
    AsmStatus st = ev(e, &op->expr, &v);
    uint32_t off = field_off(e);
    if (st != ASM_OK)
        return err(e, st);
    if (e->ctx == NULL) {
        e->pos += (size == ASM_SIZE_L) ? 4u : 2u;
        return ASM_OK;
    }
    if (v.kind == ASM_VAL_ABS) {
        if (size != ASM_SIZE_L) {
            if (size == ASM_SIZE_B ? !fit_b(v.value) : !fit_w(v.value))
                return err(e, ASM_ERR_RANGE);
            return em16(e, v.value & 0xFFFFu);
        }
        return em32(e, v.value);
    }
    {
        /* REL/EXTERN immediate: width-matched relocation, externals keep
         * the field zeroed, section-relative values stay in the bytes. */
        uint8_t type = (size == ASM_SIZE_L)   ? ASM_RELOC_ABS32
                       : (size == ASM_SIZE_W) ? ASM_RELOC_ABS16
                                              : ASM_RELOC_ABS8;
        uint32_t sym;
        int32_t addend;
        /* .b immediates keep the value in the low byte of the extension
         * word; the relocation points at the value byte (GAS parity:
         * R_68K_8 sits one past the word start). */
        uint32_t roff = (type == ASM_RELOC_ABS8) ? off + 1u : off;
        if (v.kind == ASM_VAL_EXTERN) {
            sym = v.symbol;
            addend = (int32_t)v.value;
            st = (size == ASM_SIZE_L) ? em32(e, 0) : em16(e, 0);
        } else {
            int s = asm_section_symbol(e->ctx, v.section);
            if (s < 0)
                return err(e, ASM_ERR_SYMBOL_TABLE_FULL);
            sym = (uint32_t)s;
            addend = (int32_t)v.value;
            if (size == ASM_SIZE_B   ? !fit_b(v.value)
                : size == ASM_SIZE_W ? !fit_w(v.value)
                                     : 0)
                return err(e, ASM_ERR_RANGE);
            st = (size == ASM_SIZE_L) ? em32(e, v.value)
                                      : em16(e, v.value & 0xFFFFu);
        }
        if (st != ASM_OK)
            return st;
        return add_reloc(e, type, roff, sym, addend);
    }
}

/* --- operand classification --- */

/* Classification shared by the size and encode passes. Special registers
 * (CCR/SR/USP/PC direct) are rejected here; move/link handle them before
 * calling. */
static AsmStatus op_kind(const AsmOperand* op, uint16_t* kind) {
    switch (op->mode) {
    case ASM_OP_REG:
        if (op->base_reg <= ASM_REG_D7) {
            *kind = K_DN;
            return ASM_OK;
        }
        if (op->base_reg >= ASM_REG_A0 && op->base_reg <= ASM_REG_A7) {
            *kind = K_AN;
            return ASM_OK;
        }
        return ASM_ERR_BAD_OPERAND;
    case ASM_OP_MEM:
        if (op->base_reg >= ASM_REG_A0 && op->base_reg <= ASM_REG_A7) {
            /* bare (%an) is mode 010; any explicit displacement is the
             * d16(%an) brief form with one extension word */
            *kind = op->expr.count ? K_D16AN : K_IND;
            return ASM_OK;
        }
        if (op->base_reg == ASM_REG_PC) {
            *kind = K_D16PC; /* (%pc) is treated as 0(%pc) */
            return ASM_OK;
        }
        return ASM_ERR_BAD_OPERAND;
    case ASM_OP_MEM_POSTINC:
        if (op->base_reg >= ASM_REG_A0 && op->base_reg <= ASM_REG_A7) {
            *kind = K_POST;
            return ASM_OK;
        }
        return ASM_ERR_BAD_OPERAND;
    case ASM_OP_MEM_PREDEC:
        if (op->base_reg >= ASM_REG_A0 && op->base_reg <= ASM_REG_A7) {
            *kind = K_PRE;
            return ASM_OK;
        }
        return ASM_ERR_BAD_OPERAND;
    case ASM_OP_INDEX:
        if (op->index_reg > ASM_REG_A7)
            return ASM_ERR_BAD_OPERAND;
        if (op->base_reg >= ASM_REG_A0 && op->base_reg <= ASM_REG_A7) {
            *kind = K_IXAN;
            return ASM_OK;
        }
        if (op->base_reg == ASM_REG_PC) {
            *kind = K_IXPC;
            return ASM_OK;
        }
        return ASM_ERR_BAD_OPERAND;
    case ASM_OP_ABS:
        if (op->abs_size == ASM_SIZE_W) {
            *kind = K_ABSW;
        } else {
            *kind = K_ABSL; /* unsized absolute always encodes .l */
        }
        return ASM_OK;
    case ASM_OP_IMM: *kind = K_IMM; return ASM_OK;
    default: return ASM_ERR_BAD_OPERAND;
    }
}

/* mode/reg bits for the opcode word */
static uint32_t ea_bits(uint16_t kind, const AsmOperand* op) {
    uint8_t an = (uint8_t)(op->base_reg - ASM_REG_A0);
    switch (kind) {
    case K_DN: return op->base_reg;
    case K_AN: return (1u << 3) | an;
    case K_IND: return (2u << 3) | an;
    case K_POST: return (3u << 3) | an;
    case K_PRE: return (4u << 3) | an;
    case K_D16AN: return (5u << 3) | an;
    case K_IXAN: return (6u << 3) | an;
    case K_ABSW: return (7u << 3) | 0u;
    case K_ABSL: return (7u << 3) | 1u;
    case K_D16PC: return (7u << 3) | 2u;
    case K_IXPC: return (7u << 3) | 3u;
    case K_IMM: return (7u << 3) | 4u;
    default: return 0;
    }
}

static uint32_t an_of(const AsmOperand* op) {
    return (uint32_t)(op->base_reg - ASM_REG_A0);
}

/* Extension words following the opcode, in operand order. */
static AsmStatus emit_ext(Enc* e, const AsmOperand* op, uint16_t kind,
                          uint8_t imm_size) {
    switch (kind) {
    case K_D16AN: return field_abs_dsp(e, &op->expr, ASM_RELOC_ABS16, 1);
    case K_D16PC: return field_pc16(e, &op->expr);
    case K_IXAN: return field_ix_an(e, op);
    case K_IXPC: return field_ix_pc(e, op);
    case K_ABSW: return field_abs(e, &op->expr, ASM_RELOC_ABS16);
    case K_ABSL: return field_abs(e, &op->expr, ASM_RELOC_ABS32);
    case K_IMM: return field_imm(e, op, imm_size);
    default: return ASM_OK; /* no extension word */
    }
}

static int is_dn(const AsmOperand* op) {
    return op->mode == ASM_OP_REG && op->base_reg <= ASM_REG_D7;
}

static int is_an(const AsmOperand* op) {
    return op->mode == ASM_OP_REG && op->base_reg >= ASM_REG_A0 &&
           op->base_reg <= ASM_REG_A7;
}

static int is_special_reg(const AsmOperand* op) {
    return op->mode == ASM_OP_REG && op->base_reg >= ASM_REG_PC;
}

static AsmStatus check_count(const AsmStmt* s, uint8_t want) {
    return s->op_count == want ? ASM_OK : ASM_ERR_OPERAND_COUNT;
}

static uint16_t reverse16(uint16_t v) {
    uint16_t r = 0;
    uint32_t i;
    for (i = 0; i < 16; i++) {
        if (v & (uint16_t)(1u << i))
            r |= (uint16_t)(1u << (15 - i));
    }
    return r;
}

/* Size-suffix validation. Note ASM_SIZE_B/W/L are 1/2/3 (not distinct
 * bits), so allowed suffixes are expressed as SZF_* flags. */
#define SZF_B 0x01u
#define SZF_W 0x02u
#define SZF_L 0x04u
#define SZF_NONE 0x08u
static AsmStatus check_size(Enc* e, const AsmStmt* s, uint8_t want) {
    uint8_t need;
    switch (s->size) {
    case ASM_SIZE_NONE: need = SZF_NONE; break;
    case ASM_SIZE_B: need = SZF_B; break;
    case ASM_SIZE_W: need = SZF_W; break;
    case ASM_SIZE_L: need = SZF_L; break;
    default: return err(e, ASM_ERR_BAD_SIZE);
    }
    if (want & need)
        return ASM_OK;
    return err(e, ASM_ERR_BAD_SIZE);
}

/* Evaluate an absolute quick-form immediate (addq/subq/shift count,
 * moveq data, trap vector, bit number). */
static AsmStatus quick_value(Enc* e, const AsmOperand* op, uint32_t* out) {
    AsmValue v;
    AsmStatus st = ev(e, &op->expr, &v);
    if (st != ASM_OK)
        return err(e, st);
    if (e->ctx == NULL) {
        *out = 0;
        return ASM_OK;
    }
    if (v.kind != ASM_VAL_ABS)
        return err(e, ASM_ERR_NOT_ABSOLUTE);
    *out = v.value;
    return ASM_OK;
}

/* --- instruction handlers --- */

static AsmStatus enc_move(Enc* e) {
    const AsmStmt* s = e->stmt;
    const AsmOperand* src = &s->ops[0];
    const AsmOperand* dst = &s->ops[1];
    uint16_t sk, dk;
    AsmStatus st;
    if (s->op_count != 2)
        return err(e, ASM_ERR_OPERAND_COUNT);

    /* special register forms first */
    if (dst->mode == ASM_OP_REG && dst->base_reg == ASM_REG_CCR) {
        st = check_size(e, s, SZF_NONE | SZF_W);
        if (st != ASM_OK)
            return st;
        st = op_kind(src, &sk);
        if (st != ASM_OK)
            return err(e, st);
        if (!(G_DATA & sk))
            return err(e, ASM_ERR_BAD_OPERAND);
        st = em16(e, 0x44C0u | ea_bits(sk, src));
        if (st != ASM_OK)
            return st;
        return emit_ext(e, src, sk, ASM_SIZE_W);
    }
    if (dst->mode == ASM_OP_REG && dst->base_reg == ASM_REG_SR) {
        st = check_size(e, s, SZF_NONE | SZF_W);
        if (st != ASM_OK)
            return st;
        st = op_kind(src, &sk);
        if (st != ASM_OK)
            return err(e, st);
        if (!(G_DATA & sk))
            return err(e, ASM_ERR_BAD_OPERAND);
        st = em16(e, 0x46C0u | ea_bits(sk, src));
        if (st != ASM_OK)
            return st;
        return emit_ext(e, src, sk, ASM_SIZE_W);
    }
    if (src->mode == ASM_OP_REG && src->base_reg == ASM_REG_CCR)
        return err(e, ASM_ERR_UNSUPPORTED); /* MOVE from CCR is 68010+ */
    if (src->mode == ASM_OP_REG && src->base_reg == ASM_REG_SR) {
        st = check_size(e, s, SZF_NONE | SZF_W);
        if (st != ASM_OK)
            return st;
        st = op_kind(dst, &dk);
        if (st != ASM_OK)
            return err(e, st);
        if (!(G_DALT & dk))
            return err(e, ASM_ERR_BAD_OPERAND);
        st = em16(e, 0x40C0u | ea_bits(dk, dst));
        if (st != ASM_OK)
            return st;
        return emit_ext(e, dst, dk, ASM_SIZE_W);
    }
    if (src->mode == ASM_OP_REG && src->base_reg == ASM_REG_USP) {
        if (!is_an(dst))
            return err(e, ASM_ERR_BAD_OPERAND);
        st = check_size(e, s, SZF_NONE | SZF_L);
        if (st != ASM_OK)
            return st;
        return em16(e, 0x4E68u | an_of(dst));
    }
    if (dst->mode == ASM_OP_REG && dst->base_reg == ASM_REG_USP) {
        if (!is_an(src))
            return err(e, ASM_ERR_BAD_OPERAND);
        st = check_size(e, s, SZF_NONE | SZF_L);
        if (st != ASM_OK)
            return st;
        return em16(e, 0x4E60u | an_of(src));
    }
    if (is_special_reg(src) || is_special_reg(dst))
        return err(e, ASM_ERR_BAD_OPERAND);

    st = check_size(e, s, SZF_B | SZF_W | SZF_L);
    if (st != ASM_OK)
        return st;
    st = op_kind(src, &sk);
    if (st != ASM_OK)
        return err(e, st);
    st = op_kind(dst, &dk);
    if (st != ASM_OK)
        return err(e, st);
    if (!(G_ALTER & dk) || !(G_DATA_AN & sk))
        return err(e, ASM_ERR_BAD_OPERAND);
    if (s->size == ASM_SIZE_B && (sk == K_AN || dk == K_AN))
        return err(e, ASM_ERR_BAD_OPERAND); /* byte move to/from An */

    if (dk == K_AN) { /* word/long move to An encodes as movea */
        st = em16(e, (move_size_code(s->size) << 12) | (an_of(dst) << 9) |
                         0x40u | ea_bits(sk, src));
        if (st != ASM_OK)
            return st;
        return emit_ext(e, src, sk, s->size);
    }
    /* MOVE dst field: register number bits 11-9, mode bits 8-6 */
    {
        uint32_t d = ea_bits(dk, dst);
        st = em16(e, (move_size_code(s->size) << 12) | ((d & 7u) << 9) |
                         ((d & 0x38u) << 3) | ea_bits(sk, src));
    }
    if (st != ASM_OK)
        return st;
    st = emit_ext(e, src, sk, s->size);
    if (st != ASM_OK)
        return st;
    return emit_ext(e, dst, dk, s->size);
}

static AsmStatus enc_movea(Enc* e) {
    const AsmStmt* s = e->stmt;
    uint16_t sk;
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_W | SZF_L);
    if (st != ASM_OK)
        return st;
    if (!is_an(&s->ops[1]))
        return err(e, ASM_ERR_BAD_OPERAND);
    st = op_kind(&s->ops[0], &sk);
    if (st != ASM_OK)
        return err(e, st);
    if (!(G_DATA_AN & sk))
        return err(e, ASM_ERR_BAD_OPERAND);
    st = em16(e, (move_size_code(s->size) << 12) | (an_of(&s->ops[1]) << 9) |
                     0x40u | ea_bits(sk, &s->ops[0]));
    if (st != ASM_OK)
        return st;
    return emit_ext(e, &s->ops[0], sk, s->size);
}

static AsmStatus enc_moveq(Enc* e) {
    const AsmStmt* s = e->stmt;
    uint32_t v;
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_NONE);
    if (st != ASM_OK)
        return st;
    if (s->ops[0].mode != ASM_OP_IMM || !is_dn(&s->ops[1]))
        return err(e, ASM_ERR_BAD_OPERAND);
    st = quick_value(e, &s->ops[0], &v);
    if (st != ASM_OK)
        return st;
    if (e->ctx != NULL && ((int32_t)v < -128 || (int32_t)v > 127))
        return err(e, ASM_ERR_RANGE);
    return em16(e, 0x7000u | ((uint32_t)s->ops[1].base_reg << 9) | (v & 0xFFu));
}

static AsmStatus enc_movem(Enc* e) {
    const AsmStmt* s = e->stmt;
    const AsmOperand* a = &s->ops[0];
    const AsmOperand* b = &s->ops[1];
    uint16_t kind;
    uint16_t mask;
    uint32_t opcode;
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_W | SZF_L);
    if (st != ASM_OK)
        return st;
    if (a->mode == ASM_OP_REGLIST && b->mode == ASM_OP_REGLIST)
        return err(e, ASM_ERR_BAD_OPERAND);
    if (a->mode == ASM_OP_REGLIST) { /* registers to memory */
        st = op_kind(b, &kind);
        if (st != ASM_OK)
            return err(e, st);
        if (!((K_IND | K_PRE | K_D16AN | K_IXAN | K_ABSW | K_ABSL) & kind))
            return err(e, ASM_ERR_BAD_OPERAND);
        opcode = (s->size == ASM_SIZE_L ? 0x48C0u : 0x4880u) | ea_bits(kind, b);
        mask = (kind == K_PRE) ? reverse16(a->reg_mask) : a->reg_mask;
        st = em16(e, opcode);
        if (st != ASM_OK)
            return st;
        st = em16(e, mask);
        if (st != ASM_OK)
            return st;
        return emit_ext(e, b, kind, s->size);
    }
    if (b->mode == ASM_OP_REGLIST) { /* memory to registers */
        st = op_kind(a, &kind);
        if (st != ASM_OK)
            return err(e, st);
        if (!((K_IND | K_POST | K_D16AN | K_IXAN | K_ABSW | K_ABSL | K_D16PC |
               K_IXPC) &
              kind))
            return err(e, ASM_ERR_BAD_OPERAND);
        opcode = (s->size == ASM_SIZE_L ? 0x4CC0u : 0x4C80u) | ea_bits(kind, a);
        st = em16(e, opcode);
        if (st != ASM_OK)
            return st;
        st = em16(e, b->reg_mask);
        if (st != ASM_OK)
            return st;
        return emit_ext(e, a, kind, s->size);
    }
    return err(e, ASM_ERR_BAD_OPERAND);
}

static AsmStatus enc_movep(Enc* e) {
    const AsmStmt* s = e->stmt;
    const AsmOperand* a = &s->ops[0];
    const AsmOperand* b = &s->ops[1];
    uint32_t sz = (s->size == ASM_SIZE_L) ? 0x40u : 0u;
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_W | SZF_L);
    if (st != ASM_OK)
        return st;
    if (is_dn(a) && b->mode == ASM_OP_MEM && b->base_reg >= ASM_REG_A0 &&
        b->base_reg <= ASM_REG_A7 && b->expr.count) { /* Dn to d16(An) */
        st = em16(e, 0x0100u | ((uint32_t)a->base_reg << 9) | 0x80u | sz |
                         0x08u | an_of(b));
        if (st != ASM_OK)
            return st;
        return field_abs_dsp(e, &b->expr, ASM_RELOC_ABS16, 1);
    }
    if (a->mode == ASM_OP_MEM && a->base_reg >= ASM_REG_A0 &&
        a->base_reg <= ASM_REG_A7 && a->expr.count &&
        is_dn(b)) { /* d16(An) to Dn */
        st = em16(e, 0x0100u | ((uint32_t)b->base_reg << 9) | sz | 0x08u |
                         an_of(a));
        if (st != ASM_OK)
            return st;
        return field_abs_dsp(e, &a->expr, ASM_RELOC_ABS16, 1);
    }
    return err(e, ASM_ERR_BAD_OPERAND);
}

/* lea (two operands, control source to %an) and pea (one control
 * operand) share the control-mode validation. */
static AsmStatus enc_lea_pea(Enc* e, uint32_t base, uint8_t ops) {
    const AsmStmt* s = e->stmt;
    uint16_t kind;
    uint32_t opcode = base;
    AsmStatus st = check_count(s, ops);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_NONE);
    if (st != ASM_OK)
        return st;
    st = op_kind(&s->ops[0], &kind);
    if (st != ASM_OK)
        return err(e, st);
    if (!(G_CTRL & kind))
        return err(e, ASM_ERR_BAD_OPERAND);
    if (ops == 2) { /* lea: destination %an */
        if (!is_an(&s->ops[1]))
            return err(e, ASM_ERR_BAD_OPERAND);
        opcode |= an_of(&s->ops[1]) << 9;
    }
    st = em16(e, opcode | ea_bits(kind, &s->ops[0]));
    if (st != ASM_OK)
        return st;
    return emit_ext(e, &s->ops[0], kind, ASM_SIZE_NONE);
}

/* clr/neg/negx/not/tst/nbcd/tas: one data-alterable operand. */
static AsmStatus enc_unary(Enc* e, uint32_t base, uint8_t size_mask,
                           uint8_t imm_size) {
    const AsmStmt* s = e->stmt;
    uint16_t kind;
    AsmStatus st = check_count(s, 1);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, size_mask);
    if (st != ASM_OK)
        return st;
    st = op_kind(&s->ops[0], &kind);
    if (st != ASM_OK)
        return err(e, st);
    if (!(G_DALT & kind))
        return err(e, ASM_ERR_BAD_OPERAND);
    st = em16(e, base | SIZE_BITS(s->size) | ea_bits(kind, &s->ops[0]));
    if (st != ASM_OK)
        return st;
    return emit_ext(e, &s->ops[0], kind, imm_size);
}

static AsmStatus enc_exg(Enc* e) {
    const AsmStmt* s = e->stmt;
    const AsmOperand* a = &s->ops[0];
    const AsmOperand* b = &s->ops[1];
    uint32_t op;
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_NONE);
    if (st != ASM_OK)
        return st;
    if (is_dn(a) && is_dn(b)) {
        op = 0xC140u | ((uint32_t)a->base_reg << 9) | (uint32_t)b->base_reg;
    } else if (is_an(a) && is_an(b)) {
        op = 0xC148u | (an_of(a) << 9) | an_of(b);
    } else if (is_dn(a) && is_an(b)) {
        op = 0xC188u | ((uint32_t)a->base_reg << 9) | an_of(b);
    } else {
        return err(e, ASM_ERR_BAD_OPERAND); /* %an,%dn form is invalid */
    }
    return em16(e, op);
}

static AsmStatus enc_swap(Enc* e) {
    const AsmStmt* s = e->stmt;
    AsmStatus st = check_count(s, 1);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_NONE);
    if (st != ASM_OK)
        return st;
    if (!is_dn(&s->ops[0]))
        return err(e, ASM_ERR_BAD_OPERAND);
    return em16(e, 0x4840u | (uint32_t)s->ops[0].base_reg);
}

static AsmStatus enc_ext(Enc* e) {
    const AsmStmt* s = e->stmt;
    AsmStatus st = check_count(s, 1);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_W | SZF_L);
    if (st != ASM_OK)
        return st;
    if (!is_dn(&s->ops[0]))
        return err(e, ASM_ERR_BAD_OPERAND);
    return em16(e, (s->size == ASM_SIZE_L ? 0x48C0u : 0x4880u) |
                       (uint32_t)s->ops[0].base_reg);
}

static AsmStatus enc_link(Enc* e) {
    const AsmStmt* s = e->stmt;
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_NONE);
    if (st != ASM_OK)
        return st;
    if (!is_an(&s->ops[0]) || s->ops[1].mode != ASM_OP_IMM)
        return err(e, ASM_ERR_BAD_OPERAND);
    st = em16(e, 0x4E50u | an_of(&s->ops[0]));
    if (st != ASM_OK)
        return st;
    return field_imm(e, &s->ops[1], ASM_SIZE_W);
}

static AsmStatus enc_unlink(Enc* e) {
    const AsmStmt* s = e->stmt;
    AsmStatus st = check_count(s, 1);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_NONE);
    if (st != ASM_OK)
        return st;
    if (!is_an(&s->ops[0]))
        return err(e, ASM_ERR_BAD_OPERAND);
    return em16(e, 0x4E58u | an_of(&s->ops[0]));
}

/* add/sub/and/or/cmp: Dn,<ea> and <ea>,Dn forms. Immediate sources are
 * rejected: users must write addi/subq/etc. (contract). add/sub/cmp with
 * an An destination convert to the adda/suba/cmpa form; and/or take no
 * An anywhere; cmp has no Dn,<ea> direction on the 68000. */
static AsmStatus enc_arith(Enc* e, uint32_t base, uint32_t abase,
                           uint32_t src_mask, int dn_to_ea, int an_dst) {
    const AsmStmt* s = e->stmt;
    const AsmOperand* src = &s->ops[0];
    const AsmOperand* dst = &s->ops[1];
    uint16_t sk, dk;
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_B | SZF_W | SZF_L);
    if (st != ASM_OK)
        return st;
    st = op_kind(src, &sk);
    if (st != ASM_OK)
        return err(e, st);
    st = op_kind(dst, &dk);
    if (st != ASM_OK)
        return err(e, st);
    if (sk == K_IMM)
        return err(e, ASM_ERR_UNSUPPORTED);
    if (s->size == ASM_SIZE_B && (sk == K_AN || dk == K_AN))
        return err(e, ASM_ERR_BAD_OPERAND); /* An direct is word/long */
    if (!(src_mask & sk))
        return err(e, ASM_ERR_BAD_OPERAND);
    if (dk == K_AN) { /* adda/suba/cmpa conversion */
        if (!an_dst)
            return err(e, ASM_ERR_BAD_OPERAND);
        /* opmode 011 (.w) in the base; .l sets bit 8 (opmode 111) */
        st = em16(e, abase | ((s->size == ASM_SIZE_L) ? 0x100u : 0u) |
                         (an_of(dst) << 9) | ea_bits(sk, src));
        if (st != ASM_OK)
            return st;
        return emit_ext(e, src, sk, s->size);
    }
    if (dn_to_ea && is_dn(src) && (G_DALT & dk) && dk != K_DN) { /* Dn,<ea> */
        st = em16(e, base | ((uint32_t)src->base_reg << 9) | 0x100u |
                         SIZE_BITS(s->size) | ea_bits(dk, dst));
        if (st != ASM_OK)
            return st;
        return emit_ext(e, dst, dk, s->size);
    }
    if (dk == K_DN) { /* <ea>,Dn */
        st = em16(e, base | ((uint32_t)dst->base_reg << 9) |
                         SIZE_BITS(s->size) | ea_bits(sk, src));
        if (st != ASM_OK)
            return st;
        return emit_ext(e, src, sk, s->size);
    }
    return err(e, ASM_ERR_BAD_OPERAND);
}

static AsmStatus enc_adda(Enc* e, uint32_t base) {
    const AsmStmt* s = e->stmt;
    uint16_t sk;
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_W | SZF_L);
    if (st != ASM_OK)
        return st;
    if (!is_an(&s->ops[1]))
        return err(e, ASM_ERR_BAD_OPERAND);
    st = op_kind(&s->ops[0], &sk);
    if (st != ASM_OK)
        return err(e, st);
    if (!(G_DATA_AN & sk))
        return err(e, ASM_ERR_BAD_OPERAND);
    /* opmode 011 (.w) in the base; .l sets bit 8 (opmode 111) */
    st = em16(e, base | ((s->size == ASM_SIZE_L) ? 0x100u : 0u) |
                     (an_of(&s->ops[1]) << 9) | ea_bits(sk, &s->ops[0]));
    if (st != ASM_OK)
        return st;
    return emit_ext(e, &s->ops[0], sk, s->size);
}

/* addi/subi/andi/ori/eori/cmpi: immediate source, data-alterable dest. */
static AsmStatus enc_imm_ea(Enc* e, uint32_t base) {
    const AsmStmt* s = e->stmt;
    uint16_t dk;
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_B | SZF_W | SZF_L);
    if (st != ASM_OK)
        return st;
    if (s->ops[0].mode != ASM_OP_IMM)
        return err(e, ASM_ERR_BAD_OPERAND);
    st = op_kind(&s->ops[1], &dk);
    if (st != ASM_OK)
        return err(e, st);
    if (!(G_DALT & dk))
        return err(e, ASM_ERR_BAD_OPERAND);
    /* special registers never reach here: op_kind rejects CCR/SR/USP
     * direct operands before the class check */
    st = em16(e, base | SIZE_BITS(s->size) | ea_bits(dk, &s->ops[1]));
    if (st != ASM_OK)
        return st;
    st = field_imm(e, &s->ops[0], s->size);
    if (st != ASM_OK)
        return st;
    return emit_ext(e, &s->ops[1], dk, s->size);
}

/* addq/subq: 0101 ccc 0/1 ss <ea>; addq and subq differ in bit 8. */
static AsmStatus enc_quick(Enc* e, uint32_t base) {
    const AsmStmt* s = e->stmt;
    const AsmOperand* dst = &s->ops[1];
    uint16_t dk;
    uint32_t v;
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_B | SZF_W | SZF_L);
    if (st != ASM_OK)
        return st;
    if (s->ops[0].mode != ASM_OP_IMM)
        return err(e, ASM_ERR_BAD_OPERAND);
    st = op_kind(dst, &dk);
    if (st != ASM_OK)
        return err(e, st);
    if (!(G_ALTER & dk))
        return err(e, ASM_ERR_BAD_OPERAND);
    if (s->size == ASM_SIZE_B && dk == K_AN)
        return err(e, ASM_ERR_BAD_OPERAND);
    st = quick_value(e, &s->ops[0], &v);
    if (st != ASM_OK)
        return st;
    if (e->ctx != NULL && (v < 1u || v > 8u))
        return err(e, ASM_ERR_RANGE);
    st =
        em16(e, base | ((v & 7u) << 9) | SIZE_BITS(s->size) | ea_bits(dk, dst));
    if (st != ASM_OK)
        return st;
    return emit_ext(e, dst, dk, s->size);
}

static AsmStatus enc_eor(Enc* e) {
    const AsmStmt* s = e->stmt;
    uint16_t dk;
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_B | SZF_W | SZF_L);
    if (st != ASM_OK)
        return st;
    if (s->ops[0].mode == ASM_OP_IMM)
        return err(e, ASM_ERR_UNSUPPORTED);
    if (!is_dn(&s->ops[0]))
        return err(e, ASM_ERR_BAD_OPERAND);
    st = op_kind(&s->ops[1], &dk);
    if (st != ASM_OK)
        return err(e, st);
    if (!(G_DALT & dk))
        return err(e, ASM_ERR_BAD_OPERAND);
    st = em16(e, 0xB100u | ((uint32_t)s->ops[0].base_reg << 9) | 0x100u |
                     SIZE_BITS(s->size) | ea_bits(dk, &s->ops[1]));
    if (st != ASM_OK)
        return st;
    return emit_ext(e, &s->ops[1], dk, s->size);
}

static AsmStatus enc_cmpm(Enc* e) {
    const AsmStmt* s = e->stmt;
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_B | SZF_W | SZF_L);
    if (st != ASM_OK)
        return st;
    if (s->ops[0].mode != ASM_OP_MEM_POSTINC ||
        s->ops[1].mode != ASM_OP_MEM_POSTINC ||
        s->ops[0].base_reg < ASM_REG_A0 || s->ops[0].base_reg > ASM_REG_A7 ||
        s->ops[1].base_reg < ASM_REG_A0 || s->ops[1].base_reg > ASM_REG_A7)
        return err(e, ASM_ERR_BAD_OPERAND);
    return em16(e, 0xB108u | (an_of(&s->ops[1]) << 9) | SIZE_BITS(s->size) |
                       an_of(&s->ops[0]));
}

static AsmStatus enc_muldiv(Enc* e, uint32_t base) {
    const AsmStmt* s = e->stmt;
    uint16_t sk;
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    /* bare muls/mulu/divs/divu defaults to .w; both encode identically */
    st = check_size(e, s, SZF_NONE | SZF_W);
    if (st != ASM_OK)
        return st;
    if (!is_dn(&s->ops[1]))
        return err(e, ASM_ERR_BAD_OPERAND);
    st = op_kind(&s->ops[0], &sk);
    if (st != ASM_OK)
        return err(e, st);
    if (!(G_DATA & sk))
        return err(e, ASM_ERR_BAD_OPERAND);
    st = em16(e, base | ((uint32_t)s->ops[1].base_reg << 9) |
                     ea_bits(sk, &s->ops[0]));
    if (st != ASM_OK)
        return st;
    return emit_ext(e, &s->ops[0], sk, ASM_SIZE_NONE);
}

static AsmStatus enc_bcd(Enc* e, uint32_t base) {
    const AsmStmt* s = e->stmt;
    const AsmOperand* src = &s->ops[0];
    const AsmOperand* dst = &s->ops[1];
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    /* bare form arrives as .b from the parser; the byte opmode bits are 00 */
    st = check_size(e, s, SZF_NONE | SZF_B);
    if (st != ASM_OK)
        return st;
    if (is_dn(src) && is_dn(dst))
        return em16(e, base | ((uint32_t)dst->base_reg << 9) |
                           (uint32_t)src->base_reg);
    if (src->mode == ASM_OP_MEM_PREDEC && dst->mode == ASM_OP_MEM_PREDEC)
        return em16(e, base | (an_of(dst) << 9) | 0x08u | an_of(src));
    return err(e, ASM_ERR_BAD_OPERAND);
}

static AsmStatus enc_chk(Enc* e) {
    const AsmStmt* s = e->stmt;
    uint16_t sk;
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_NONE | SZF_W); /* .l is 68020+ */
    if (st != ASM_OK)
        return st;
    if (!is_dn(&s->ops[1]))
        return err(e, ASM_ERR_BAD_OPERAND);
    st = op_kind(&s->ops[0], &sk);
    if (st != ASM_OK)
        return err(e, st);
    if (!(G_DATA & sk))
        return err(e, ASM_ERR_BAD_OPERAND);
    st = em16(e, 0x4180u | ((uint32_t)s->ops[1].base_reg << 9) |
                     ea_bits(sk, &s->ops[0]));
    if (st != ASM_OK)
        return st;
    return emit_ext(e, &s->ops[0], sk, ASM_SIZE_W);
}

/* asl/asr/lsl/lsr/rol/ror/roxl/roxr: type 0=as 1=ls 2=rox 3=ro,
 * dr 1=left 0=right. */
static AsmStatus enc_shift(Enc* e, uint32_t type, uint32_t dr) {
    const AsmStmt* s = e->stmt;
    const AsmOperand* src = &s->ops[0];
    const AsmOperand* dst = &s->ops[1];
    uint16_t kind;
    uint32_t count;
    AsmStatus st;
    if (s->op_count == 1) { /* memory form, word only */
        st = op_kind(&s->ops[0], &kind);
        if (st != ASM_OK)
            return err(e, st);
        /* a register operand means the two-operand count form: the user
         * omitted the shift count (GAS parity: asl.l (%a1) is rejected) */
        if (!(G_MEMD & kind))
            return err(e, ASM_ERR_OPERAND_COUNT);
        st = check_size(e, s, SZF_NONE | SZF_W);
        if (st != ASM_OK)
            return st;
        st = em16(e, 0xE0C0u | (type << 9) | (dr << 8) |
                         ea_bits(kind, &s->ops[0]));
        if (st != ASM_OK)
            return st;
        return emit_ext(e, &s->ops[0], kind, ASM_SIZE_W);
    }
    st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_B | SZF_W | SZF_L);
    if (st != ASM_OK)
        return st;
    if (!is_dn(dst))
        return err(e, ASM_ERR_BAD_OPERAND);
    if (src->mode == ASM_OP_IMM) { /* immediate count #1-8 */
        st = quick_value(e, src, &count);
        if (st != ASM_OK)
            return st;
        if (e->ctx != NULL && (count < 1u || count > 8u))
            return err(e, ASM_ERR_RANGE);
        count &= 7u;
        return em16(e, 0xE000u | (count << 9) | (dr << 8) | SIZE_BITS(s->size) |
                           (type << 3) | (uint32_t)dst->base_reg);
    }
    if (is_dn(src)) { /* register count */
        return em16(e, 0xE000u | ((uint32_t)src->base_reg << 9) | (dr << 8) |
                           SIZE_BITS(s->size) | 0x20u | (type << 3) |
                           (uint32_t)dst->base_reg);
    }
    return err(e, ASM_ERR_BAD_OPERAND);
}

/* bchg/bclr/bset/btst: idx 0=btst 1=bchg 2=bclr 3=bset. */
static AsmStatus enc_bitop(Enc* e, uint32_t idx) {
    const AsmStmt* s = e->stmt;
    const AsmOperand* src = &s->ops[0];
    const AsmOperand* dst = &s->ops[1];
    uint16_t dk;
    uint32_t bit;
    uint32_t dst_mask =
        (idx == 0) ? (G_MEMD | K_DN | K_D16PC | K_IXPC) : (G_MEMD | K_DN);
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    /* bare form arrives as .b from the parser (byte operation) */
    st = check_size(e, s, SZF_NONE | SZF_B);
    if (st != ASM_OK)
        return st;
    st = op_kind(dst, &dk);
    if (st != ASM_OK)
        return err(e, st);
    if (!(dst_mask & dk))
        return err(e, ASM_ERR_BAD_OPERAND);
    if (src->mode == ASM_OP_IMM) {
        st = quick_value(e, src, &bit);
        if (st != ASM_OK)
            return st;
        if (e->ctx != NULL && bit > 31u)
            return err(e, ASM_ERR_RANGE);
        st = em16(e, 0x0800u | (idx << 6) | ea_bits(dk, dst));
        if (st != ASM_OK)
            return st;
        st = em16(e, bit & 0xFFu);
        if (st != ASM_OK)
            return st;
        return emit_ext(e, dst, dk, ASM_SIZE_NONE);
    }
    if (is_dn(src)) {
        st = em16(e, ((uint32_t)src->base_reg << 9) | ((idx + 4u) << 6) |
                         ea_bits(dk, dst));
        if (st != ASM_OK)
            return st;
        return emit_ext(e, dst, dk, ASM_SIZE_NONE);
    }
    return err(e, ASM_ERR_BAD_OPERAND);
}

static AsmStatus enc_branch(Enc* e, uint8_t cc) {
    const AsmStmt* s = e->stmt;
    const AsmOperand* op = &s->ops[0];
    AsmValue v;
    AsmStatus st = check_count(s, 1);
    if (st != ASM_OK)
        return err(e, st);
    if (op->mode != ASM_OP_ABS)
        return err(e, ASM_ERR_BAD_OPERAND);
    if (s->size == ASM_SIZE_L)
        return err(e, ASM_ERR_BAD_SIZE);
    if (s->size == ASM_SIZE_B) {
        /* 8-bit displacement resolved at assembly time, same text only */
        st = ev(e, &op->expr, &v);
        if (st != ASM_OK)
            return err(e, st);
        if (e->ctx == NULL)
            return em16(e, 0);
        if (v.kind == ASM_VAL_ABS)
            return err(e, ASM_ERR_NOT_ABSOLUTE);
        if (v.kind != ASM_VAL_REL || v.section != e->ctx->section)
            return err(e, ASM_ERR_UNSUPPORTED); /* extern or cross-section */
        {
            int32_t disp = (int32_t)v.value - (int32_t)(e->start + 2u);
            if (!fit_d8((uint32_t)disp))
                return err(e, ASM_ERR_RANGE);
            return em16(e, 0x6000u | ((uint32_t)cc << 8) |
                               ((uint32_t)disp & 0xFFu));
        }
    }
    st = em16(e, 0x6000u | ((uint32_t)cc << 8));
    if (st != ASM_OK)
        return st;
    return field_pc16(e, &op->expr);
}

static AsmStatus enc_dbcc(Enc* e, uint8_t cc) {
    const AsmStmt* s = e->stmt;
    const AsmOperand* reg = &s->ops[0];
    const AsmOperand* target = &s->ops[1];
    AsmStatus st = check_count(s, 2);
    if (st != ASM_OK)
        return err(e, st);
    /* the parser defaults the bare dbcc suffix to .w (2-byte displacement) */
    st = check_size(e, s, SZF_NONE | SZF_W);
    if (st != ASM_OK)
        return st;
    if (!is_dn(reg) || target->mode != ASM_OP_ABS)
        return err(e, ASM_ERR_BAD_OPERAND);
    st = em16(e, 0x50C8u | ((uint32_t)cc << 8) | (uint32_t)reg->base_reg);
    if (st != ASM_OK)
        return st;
    return field_pc16(e, &target->expr);
}

static AsmStatus enc_scc(Enc* e, uint8_t cc) {
    const AsmStmt* s = e->stmt;
    uint16_t kind;
    AsmStatus st = check_count(s, 1);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_NONE | SZF_B);
    if (st != ASM_OK)
        return st;
    st = op_kind(&s->ops[0], &kind);
    if (st != ASM_OK)
        return err(e, st);
    if (!(G_DALT & kind))
        return err(e, ASM_ERR_BAD_OPERAND); /* no An, no imm */
    st = em16(e, 0x50C0u | ((uint32_t)cc << 8) | ea_bits(kind, &s->ops[0]));
    if (st != ASM_OK)
        return st;
    return emit_ext(e, &s->ops[0], kind, ASM_SIZE_B);
}

static AsmStatus enc_jsr_jmp(Enc* e, uint32_t base) {
    const AsmStmt* s = e->stmt;
    uint16_t kind;
    AsmStatus st = check_count(s, 1);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_NONE);
    if (st != ASM_OK)
        return st;
    st = op_kind(&s->ops[0], &kind);
    if (st != ASM_OK)
        return err(e, st);
    if (!(G_CTRL & kind))
        return err(e, ASM_ERR_BAD_OPERAND);
    st = em16(e, base | ea_bits(kind, &s->ops[0]));
    if (st != ASM_OK)
        return st;
    return emit_ext(e, &s->ops[0], kind, ASM_SIZE_NONE);
}

static AsmStatus enc_simple(Enc* e, uint32_t word) {
    AsmStatus st = check_count(e->stmt, 0);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, e->stmt, SZF_NONE);
    if (st != ASM_OK)
        return st;
    return em16(e, word);
}

static AsmStatus enc_stop(Enc* e) {
    const AsmStmt* s = e->stmt;
    uint32_t v;
    AsmStatus st = check_count(s, 1);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_NONE);
    if (st != ASM_OK)
        return st;
    if (s->ops[0].mode != ASM_OP_IMM)
        return err(e, ASM_ERR_BAD_OPERAND);
    /* validate the data word before emitting the opcode so errors leave
     * nothing emitted */
    st = quick_value(e, &s->ops[0], &v);
    if (st != ASM_OK)
        return st;
    if (e->ctx != NULL && !fit_w(v))
        return err(e, ASM_ERR_RANGE);
    st = em16(e, 0x4E72u);
    if (st != ASM_OK)
        return st;
    return field_imm(e, &s->ops[0], ASM_SIZE_W);
}

static AsmStatus enc_trap(Enc* e) {
    const AsmStmt* s = e->stmt;
    uint32_t v;
    AsmStatus st = check_count(s, 1);
    if (st != ASM_OK)
        return err(e, st);
    st = check_size(e, s, SZF_NONE);
    if (st != ASM_OK)
        return st;
    if (s->ops[0].mode != ASM_OP_IMM)
        return err(e, ASM_ERR_BAD_OPERAND);
    st = quick_value(e, &s->ops[0], &v);
    if (st != ASM_OK)
        return st;
    if (e->ctx != NULL && v > 15u)
        return err(e, ASM_ERR_RANGE);
    return em16(e, 0x4E40u | (v & 0xFu));
}

static int cc_ok(uint8_t cc) { return cc >= ASM_CC_HI && cc <= ASM_CC_LE; }

static AsmStatus encode_insn(Enc* e) {
    const AsmStmt* s = e->stmt;
    uint8_t mn = s->mnemonic;
    uint8_t cc = 0;
    if (s->kind != ASM_STMT_INSN)
        return err(e, ASM_ERR_BAD_OPERAND);

    /* condition code forms */
    if ((mn >= ASM_MN_BCC && mn <= ASM_MN_BVS) || mn == ASM_MN_BRA ||
        mn == ASM_MN_BSR) {
        if (mn == ASM_MN_BRA)
            cc = 0;
        else if (mn == ASM_MN_BSR)
            cc = 1;
        else {
            if (!cc_ok(s->cc))
                return err(e, ASM_ERR_UNSUPPORTED); /* bt/bf */
            cc = s->cc;
        }
        return enc_branch(e, cc);
    }
    if (mn >= ASM_MN_DBCC && mn <= ASM_MN_DBVS) {
        if (!cc_ok(s->cc))
            return err(e, ASM_ERR_UNSUPPORTED);
        return enc_dbcc(e, s->cc);
    }
    if (mn == ASM_MN_DBRA)
        return enc_dbcc(e, ASM_CC_F);
    if (mn >= ASM_MN_SCC && mn <= ASM_MN_SVS) {
        if (!cc_ok(s->cc))
            return err(e, ASM_ERR_UNSUPPORTED); /* st/sf */
        return enc_scc(e, s->cc);
    }

    switch (mn) {
    case ASM_MN_MOVE: return enc_move(e);
    case ASM_MN_MOVEA: return enc_movea(e);
    case ASM_MN_MOVEQ: return enc_moveq(e);
    case ASM_MN_MOVEM: return enc_movem(e);
    case ASM_MN_MOVEP: return enc_movep(e);
    case ASM_MN_LEA: return enc_lea_pea(e, 0x41C0u, 2);
    case ASM_MN_PEA: return enc_lea_pea(e, 0x4840u, 1);
    case ASM_MN_CLR:
        return enc_unary(e, 0x4200u, SZF_B | SZF_W | SZF_L, SZF_NONE);
    case ASM_MN_NEG:
        return enc_unary(e, 0x4400u, SZF_B | SZF_W | SZF_L, SZF_NONE);
    case ASM_MN_NEGX:
        return enc_unary(e, 0x4000u, SZF_B | SZF_W | SZF_L, SZF_NONE);
    case ASM_MN_NOT:
        return enc_unary(e, 0x4600u, SZF_B | SZF_W | SZF_L, SZF_NONE);
    case ASM_MN_TST:
        return enc_unary(e, 0x4A00u, SZF_B | SZF_W | SZF_L, SZF_NONE);
    case ASM_MN_NBCD: return enc_unary(e, 0x4800u, SZF_NONE | SZF_B, SZF_NONE);
    case ASM_MN_TAS: return enc_unary(e, 0x4AC0u, SZF_NONE | SZF_B, SZF_NONE);
    case ASM_MN_EXG: return enc_exg(e);
    case ASM_MN_SWAP: return enc_swap(e);
    case ASM_MN_EXT: return enc_ext(e);
    case ASM_MN_LINK: return enc_link(e);
    case ASM_MN_UNLINK: return enc_unlink(e);
    case ASM_MN_ADD: return enc_arith(e, 0xD000u, 0xD0C0u, G_DATA_AN, 1, 1);
    case ASM_MN_SUB: return enc_arith(e, 0x9000u, 0x90C0u, G_DATA_AN, 1, 1);
    case ASM_MN_CMP: return enc_arith(e, 0xB000u, 0xB0C0u, G_DATA_AN, 0, 1);
    case ASM_MN_AND: return enc_arith(e, 0xC000u, 0, G_DATA, 1, 0);
    case ASM_MN_OR: return enc_arith(e, 0x8000u, 0, G_DATA, 1, 0);
    case ASM_MN_ADDA: return enc_adda(e, 0xD0C0u);
    case ASM_MN_SUBA: return enc_adda(e, 0x90C0u);
    case ASM_MN_CMPA: return enc_adda(e, 0xB0C0u);
    case ASM_MN_ADDI: return enc_imm_ea(e, 0x0600u);
    case ASM_MN_SUBI: return enc_imm_ea(e, 0x0400u);
    case ASM_MN_ANDI: return enc_imm_ea(e, 0x0200u);
    case ASM_MN_ORI: return enc_imm_ea(e, 0x0000u);
    case ASM_MN_EORI: return enc_imm_ea(e, 0x0A00u);
    case ASM_MN_CMPI: return enc_imm_ea(e, 0x0C00u);
    case ASM_MN_ADDQ: return enc_quick(e, 0x5000u);
    case ASM_MN_SUBQ: return enc_quick(e, 0x5100u);
    case ASM_MN_EOR: return enc_eor(e);
    case ASM_MN_CMPM: return enc_cmpm(e);
    case ASM_MN_MULS: return enc_muldiv(e, 0xC1C0u);
    case ASM_MN_MULU: return enc_muldiv(e, 0xC0C0u);
    case ASM_MN_DIVS: return enc_muldiv(e, 0x81C0u);
    case ASM_MN_DIVU: return enc_muldiv(e, 0x80C0u);
    case ASM_MN_ABCD: return enc_bcd(e, 0xC100u);
    case ASM_MN_SBCD: return enc_bcd(e, 0x8100u);
    case ASM_MN_CHK: return enc_chk(e);
    case ASM_MN_ASL: return enc_shift(e, 0u, 1u);
    case ASM_MN_ASR: return enc_shift(e, 0u, 0u);
    case ASM_MN_LSL: return enc_shift(e, 1u, 1u);
    case ASM_MN_LSR: return enc_shift(e, 1u, 0u);
    case ASM_MN_ROXL: return enc_shift(e, 2u, 1u);
    case ASM_MN_ROXR: return enc_shift(e, 2u, 0u);
    case ASM_MN_ROL: return enc_shift(e, 3u, 1u);
    case ASM_MN_ROR: return enc_shift(e, 3u, 0u);
    case ASM_MN_BTST: return enc_bitop(e, 0u);
    case ASM_MN_BCHG: return enc_bitop(e, 1u);
    case ASM_MN_BCLR: return enc_bitop(e, 2u);
    case ASM_MN_BSET: return enc_bitop(e, 3u);
    case ASM_MN_JSR: return enc_jsr_jmp(e, 0x4E80u);
    case ASM_MN_JMP: return enc_jsr_jmp(e, 0x4EC0u);
    case ASM_MN_NOP: return enc_simple(e, 0x4E71u);
    case ASM_MN_RTE: return enc_simple(e, 0x4E73u);
    case ASM_MN_RTS: return enc_simple(e, 0x4E75u);
    case ASM_MN_RTR: return enc_simple(e, 0x4E77u);
    case ASM_MN_TRAPV: return enc_simple(e, 0x4E76u);
    case ASM_MN_RESET: return enc_simple(e, 0x4E70u);
    case ASM_MN_ILLEGAL: return enc_simple(e, 0x4AFCu);
    case ASM_MN_STOP: return enc_stop(e);
    case ASM_MN_TRAP: return enc_trap(e);
    default: return err(e, ASM_ERR_UNKNOWN_MNEMONIC);
    }
}

AsmStatus asm_insn_size(const AsmStmt* stmt, uint32_t* size) {
    Enc e;
    AsmStatus st;
    e.ctx = NULL;
    e.stmt = stmt;
    e.start = 0;
    e.pos = 0;
    st = encode_insn(&e);
    if (st != ASM_OK)
        return st;
    *size = e.pos;
    return ASM_OK;
}

AsmStatus asm_encode_insn(AsmCtx* ctx, const AsmStmt* stmt) {
    Enc e;
    uint32_t offset = ctx->offset[ctx->section];
    uint32_t reloc_count = ctx->reloc_count;
    AsmStatus st;
    e.ctx = ctx;
    e.stmt = stmt;
    e.start = offset;
    e.pos = 0;
    st = encode_insn(&e);
    if (st != ASM_OK) {
        /* value-dependent checks run after the opcode word is emitted;
         * roll back so an error leaves nothing emitted or relocated */
        ctx->offset[ctx->section] = offset;
        ctx->reloc_count = reloc_count;
    }
    return st;
}
