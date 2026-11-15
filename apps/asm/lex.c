/* Lexer for the SSOS-9 MC68000 assembler: splits one source line into
 * tokens per docs/assembler-contract.md. Comments run from ';' to the
 * end of the line and produce no tokens. Motorola '|' comments are not
 * supported; '|' always lexes as the OR operator. */

#include "asm.h"

static int is_space(char c) { return c == ' ' || c == '\t'; }

static int is_digit(char c) { return c >= '0' && c <= '9'; }

static int is_hex(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int hex_val(char c) {
    if (c <= '9')
        return c - '0';
    if (c >= 'a')
        return c - 'a' + 10;
    return c - 'A' + 10;
}

static char lower(char c) {
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static int is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c == '.';
}

static int is_ident_char(char c) { return is_ident_start(c) || is_digit(c); }

static int is_alnum(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* Maps a register name (text after '%', already scanned) to ASM_REG_*.
 * Returns -1 when the name is not a supported register. */
static int reg_lookup(const char* s, uint8_t len) {
    if (len == 2 && s[1] >= '0' && s[1] <= '7') {
        if (lower(s[0]) == 'd')
            return ASM_REG_D0 + (s[1] - '0');
        if (lower(s[0]) == 'a')
            return ASM_REG_A0 + (s[1] - '0');
    }
    if (len == 2) {
        if (lower(s[0]) == 's' && lower(s[1]) == 'p')
            return ASM_REG_A7;
        if (lower(s[0]) == 'p' && lower(s[1]) == 'c')
            return ASM_REG_PC;
        if (lower(s[0]) == 's' && lower(s[1]) == 'r')
            return ASM_REG_SR;
        return -1;
    }
    if (len == 3) {
        if (lower(s[0]) == 'c' && lower(s[1]) == 'c' && lower(s[2]) == 'r')
            return ASM_REG_CCR;
        if (lower(s[0]) == 'u' && lower(s[1]) == 's' && lower(s[2]) == 'p')
            return ASM_REG_USP;
    }
    return -1;
}

/* Scans digits at p (hex when hex_only) and accumulates into *value.
 * Returns the number of characters consumed; a literal that does not fit
 * in 32 bits (GAS rejects it as out of range too) sets *status to
 * ASM_ERR_RANGE and stops accumulating. */
static uint8_t scan_digits(const char* p, int hex_only, uint32_t* value,
                           AsmStatus* status) {
    uint8_t n = 0;
    uint32_t v = 0;
    *status = ASM_OK;
    while (p[n]) {
        char c = p[n];
        uint32_t d;
        if (hex_only) {
            if (!is_hex(c))
                break;
            d = (uint32_t)hex_val(c);
        } else {
            if (!is_digit(c))
                break;
            d = (uint32_t)(c - '0');
        }
        if (hex_only) {
            /* v << 4 | d must stay inside 32 bits */
            if (v > 0x0FFFFFFFu) {
                *status = ASM_ERR_RANGE;
                break;
            }
            v = (v << 4) | d;
        } else {
            if (v > (0xFFFFFFFFu - d) / 10u) {
                *status = ASM_ERR_RANGE;
                break;
            }
            v = v * 10u + d;
        }
        n++;
    }
    *value = v;
    return n;
}

AsmStatus asm_lex(const char* line, AsmToken* tokens, uint8_t max,
                  uint8_t* count) {
    const char* p = line;
    uint8_t n = 0;
    *count = 0;
    if (max == 0)
        return ASM_ERR_SYNTAX;
    for (;;) {
        const char* q;
        char c = *p;
        AsmToken* tok;
        if (c == 0)
            break;
        if (is_space(c)) {
            p++;
            continue;
        }
        if (c == ';')
            break;
        /* Keep room for the EOL terminator token. */
        if (n + 1 >= (int)max)
            return ASM_ERR_SYNTAX;
        tok = &tokens[n];
        tok->kind = ASM_TOK_EOL;
        tok->reg = 0;
        tok->length = 0;
        tok->number = 0;
        tok->text = 0;
        if (c == '%') {
            int reg;
            p++;
            q = p;
            while (is_alnum(*p)) p++;
            reg = reg_lookup(q, (uint8_t)(p - q));
            if (reg < 0)
                return ASM_ERR_UNKNOWN_REGISTER;
            tok->kind = ASM_TOK_REG;
            tok->reg = (uint8_t)reg;
            n++;
            continue;
        }
        if (c == '$') {
            uint32_t value = 0;
            uint8_t digits;
            AsmStatus st = ASM_OK;
            q = p;
            p++;
            digits = scan_digits(p, 1, &value, &st);
            if (digits == 0)
                return ASM_ERR_SYNTAX;
            if (st != ASM_OK)
                return st;
            p += digits;
            tok->kind = ASM_TOK_NUMBER;
            tok->number = value;
            tok->text = q;
            tok->length = (uint8_t)(p - q);
            n++;
            continue;
        }
        if (is_digit(c)) {
            uint32_t value = 0;
            uint8_t digits;
            AsmStatus st = ASM_OK;
            q = p;
            if (c == '0' && (p[1] == 'x' || p[1] == 'X')) {
                /* 0x always commits to hex; a missing digit is an
                 * error rather than "0" followed by an identifier. */
                p += 2;
                digits = scan_digits(p, 1, &value, &st);
                if (digits == 0)
                    return ASM_ERR_SYNTAX;
                if (st != ASM_OK)
                    return st;
                p += digits;
            } else {
                digits = scan_digits(p, 0, &value, &st);
                if (st != ASM_OK)
                    return st;
                p += digits;
                /* Numeric label reference: single digit + b/f, not
                 * followed by more identifier characters. */
                if (digits == 1 &&
                    (*p == 'b' || *p == 'B' || *p == 'f' || *p == 'F') &&
                    !is_ident_char(p[1])) {
                    p++;
                    tok->kind = ASM_TOK_IDENT;
                    tok->text = q;
                    tok->length = 2;
                    n++;
                    continue;
                }
            }
            tok->kind = ASM_TOK_NUMBER;
            tok->number = value;
            tok->text = q;
            tok->length = (uint8_t)(p - q);
            n++;
            continue;
        }
        if (c == '\'') {
            q = p;
            p++;
            if (*p == 0)
                return ASM_ERR_SYNTAX;
            tok->kind = ASM_TOK_NUMBER;
            tok->number = (uint8_t)*p;
            tok->text = q;
            tok->length = 2;
            p++;
            /* GAS-style closed form 'a': absorb the closing quote when
             * present ('a', lexes as 97 then whatever follows). The
             * unterminated 'a form stays valid (contract primary form). */
            if (*p == '\'')
                p++;
            n++;
            continue;
        }
        if (c == '"') {
            q = p;
            p++;
            while (*p && *p != '"') p++;
            if (*p == 0)
                return ASM_ERR_SYNTAX;
            tok->kind = ASM_TOK_STRING;
            tok->text = q + 1; /* raw bytes without the quotes */
            tok->length = (uint8_t)(p - q - 1);
            p++;
            n++;
            continue;
        }
        if (is_ident_start(c)) {
            q = p;
            p++;
            while (is_ident_char(*p)) p++;
            if (p - q > ASM_NAME_MAX)
                return ASM_ERR_SYNTAX;
            tok->kind = ASM_TOK_IDENT;
            tok->text = q;
            tok->length = (uint8_t)(p - q);
            n++;
            continue;
        }
        if (c == '<' && p[1] == '<') {
            tok->kind = ASM_TOK_SHL;
            p += 2;
            n++;
            continue;
        }
        if (c == '>' && p[1] == '>') {
            tok->kind = ASM_TOK_SHR;
            p += 2;
            n++;
            continue;
        }
        switch (c) {
        case '+': tok->kind = ASM_TOK_PLUS; break;
        case '-': tok->kind = ASM_TOK_MINUS; break;
        case '*': tok->kind = ASM_TOK_STAR; break;
        case '/': tok->kind = ASM_TOK_SLASH; break;
        case '&': tok->kind = ASM_TOK_AND; break;
        case '|': tok->kind = ASM_TOK_OR; break;
        case '^': tok->kind = ASM_TOK_XOR; break;
        case '~': tok->kind = ASM_TOK_NOT; break;
        case '(': tok->kind = ASM_TOK_LPAREN; break;
        case ')': tok->kind = ASM_TOK_RPAREN; break;
        case ',': tok->kind = ASM_TOK_COMMA; break;
        case ':': tok->kind = ASM_TOK_COLON; break;
        case '#': tok->kind = ASM_TOK_HASH; break;
        default: return ASM_ERR_SYNTAX;
        }
        p++;
        n++;
    }
    tokens[n].kind = ASM_TOK_EOL;
    *count = n;
    return ASM_OK;
}
