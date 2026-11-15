/* Host driver for the assembler core: same two-pass pipeline as the S9M1
 * module, with stdio file access. Used by unit and golden tests. */

#include "asm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int file_sink(const void* bytes, uint32_t length, void* user) {
    return fwrite(bytes, 1, length, (FILE*)user) == length ? 0 : 1;
}

/* Non-fatal core warnings (e.g. .dc.w symbol in .text) go to stderr. */
static void warn_stderr(void* user, uint32_t line, const char* message) {
    fprintf(stderr, "%s:%u: warning: %s\n", (const char*)user, line, message);
}

/* Numeric branch operands are literal displacements (contract, A-01): the
 * operand value is written unmodified into the displacement field, with no
 * relocation. GAS instead emits R_68K_PC16/PC8 against *ABS*, so this is
 * pinned through the core alone, not through a golden case. */
static int branch_test(void) {
    static const char* lines[] = {
        ".text", "bsr.w 0x10", "nop", "bne.w 0x20", "nop", ".end",
    };
    static AsmCtx ctx;
    unsigned i, j;
    AsmStatus status = ASM_OK;
    asm_ctx_init(&ctx);
    for (i = 0; i < 2 && status == ASM_OK; i++) {
        asm_assemble_begin(&ctx, i + 1u);
        for (j = 0; j < sizeof(lines) / sizeof(lines[0]); j++)
            status = asm_assemble_line(&ctx, lines[j]);
        if (status == ASM_OK)
            status = asm_assemble_end(&ctx);
    }
    if (status != ASM_OK) {
        fprintf(stderr, "selftest: branch source failed: %s\n",
                asm_status_message(ctx.status));
        return 1;
    }
    if (ctx.offset[ASM_SECTION_TEXT] != 12 ||
        ctx.text[0] != 0x61 || ctx.text[1] != 0x00 ||
        ctx.text[2] != 0x00 || ctx.text[3] != 0x10 ||
        ctx.text[6] != 0x66 || ctx.text[7] != 0x00 ||
        ctx.text[8] != 0x00 || ctx.text[9] != 0x20 ||
        ctx.reloc_count != 0) {
        fprintf(stderr,
                "selftest: numeric branch bytes wrong: "
                "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
                "relocs %u\n",
                ctx.text[0], ctx.text[1], ctx.text[2], ctx.text[3],
                ctx.text[4], ctx.text[5], ctx.text[6], ctx.text[7],
                ctx.text[8], ctx.text[9], ctx.reloc_count);
        return 1;
    }
    return 0;
}

/* Unit checks for asm_default_output (apps/asm/assemble.c) and the core
 * warning hook, which are not linked into the parse/encode unit builds.
 * Run with: asm-host --selftest */
static char warn_log[512];

static void warn_capture(void* user, uint32_t line, const char* message) {
    (void)user;
    snprintf(warn_log + strlen(warn_log), sizeof(warn_log) - strlen(warn_log),
             "%u:%s;", line, message);
}

static int warn_test(void) {
    static const char* lines[] = {
        ".text", "nop", ".dc.w foo", /* warns: line 3 */
        ".data", ".dc.w foo",        /* data section: no warning */
        ".text", "foo:", "nop", ".end",
    };
    static AsmCtx ctx;
    unsigned i;
    AsmStatus status = ASM_OK;
    warn_log[0] = 0;
    asm_ctx_init(&ctx);
    asm_set_warn(&ctx, warn_capture, 0);
    for (i = 0; i < 2 && status == ASM_OK; i++) {
        unsigned j;
        asm_assemble_begin(&ctx, i + 1u);
        for (j = 0; j < sizeof(lines) / sizeof(lines[0]); j++)
            status = asm_assemble_line(&ctx, lines[j]);
        if (status == ASM_OK)
            status = asm_assemble_end(&ctx);
    }
    if (status != ASM_OK) {
        fprintf(stderr, "selftest: warn source failed: %s\n",
                asm_status_message(ctx.status));
        return 1;
    }
    if (strcmp(warn_log,
               "3:.dc.w symbol in text reads as A5 displacement (PIC);") !=
        0) {
        fprintf(stderr, "selftest: unexpected warning log [%s]\n", warn_log);
        return 1;
    }
    /* the warning never changes the object: one ABS16 in text, one in data */
    if (ctx.reloc_count != 2) {
        fprintf(stderr, "selftest: reloc count %u (want 2)\n", ctx.reloc_count);
        return 1;
    }
    return 0;
}

static int self_test(void) {
    static const struct {
        const char* source;
        const char* want;
    } ok_cases[] = {
        {"a.s", "a.O"},
        {"foo", "foo.O"},
        {"12345678.s", "12345678.O"},
    };
    static const char* reject_cases[] = {
        "123456789.s", /* base too long */
        "a.b.c",       /* derived base "a.b" still contains a dot */
        "x.y.z",
    };
    char out[16];
    size_t i;
    int failures = 0;
    for (i = 0; i < sizeof(ok_cases) / sizeof(ok_cases[0]); i++) {
        if (asm_default_output(ok_cases[i].source, out) != ASM_OK ||
            strcmp(out, ok_cases[i].want) != 0) {
            fprintf(stderr, "selftest: %s -> %s (want %s)\n",
                    ok_cases[i].source,
                    asm_default_output(ok_cases[i].source, out) == ASM_OK
                        ? out
                        : "(rejected)",
                    ok_cases[i].want);
            failures++;
        }
    }
    for (i = 0; i < sizeof(reject_cases) / sizeof(reject_cases[0]); i++) {
        if (asm_default_output(reject_cases[i], out) != ASM_ERR_NAME_LONG) {
            fprintf(stderr, "selftest: %s accepted (want NAME_LONG)\n",
                    reject_cases[i]);
            failures++;
        }
    }
    return (failures || warn_test() || branch_test()) ? 1 : 0;
}

static int run_source(AsmCtx* ctx, const char* path) {
    FILE* file = fopen(path, "r");
    char line[512];
    if (!file) {
        fprintf(stderr, "%s: cannot open\n", path);
        return -1;
    }
    for (;;) {
        uint32_t n = 0;
        int eof = 0;
        /* Character-based reading mirrors the on-target reader: CR bytes
         * are skipped anywhere in the line, lines of 200 bytes or more are
         * rejected so escape-resolved strings cannot exceed the statement
         * storage (ASM_LINE_MAX), and a NUL byte inside a line is an error
         * instead of silently terminating the line. */
        for (;;) {
            int c = fgetc(file);
            if (c == EOF) {
                eof = 1;
                break;
            }
            if (c == '\n')
                break;
            if (c == '\r')
                continue;
            if (c == 0) {
                fprintf(stderr, "%s:%u: NUL byte in source line\n", path,
                        ctx->line + 1u);
                fclose(file);
                return -1;
            }
            if (n >= ASM_LINE_MAX) {
                fprintf(stderr, "%s:%u: line too long\n", path,
                        ctx->line + 1u);
                fclose(file);
                return -1;
            }
            line[n++] = (char)c;
        }
        line[n] = 0;
        if (eof && n == 0)
            break;
        if (asm_assemble_line(ctx, line) != ASM_OK) {
            fprintf(stderr, "%s:%u: %s\n", path, ctx->error_line,
                    asm_status_message(ctx->status));
            fclose(file);
            return -1;
        }
        if (ctx->ended)
            break;
    }
    fclose(file);
    if (asm_assemble_end(ctx) != ASM_OK) {
        fprintf(stderr, "%s:%u: %s\n", path, ctx->error_line,
                asm_status_message(ctx->status));
        return -1;
    }
    return 0;
}

int main(int argc, char** argv) {
    static AsmCtx ctx;
    char output_name[16];
    const char* destination;
    FILE* out;
    uint32_t pass;
    if (argc == 2 && strcmp(argv[1], "--selftest") == 0)
        return self_test();
    if (argc == 3) {
        destination = argv[2];
    } else if (argc == 2) {
        /* Default output name, same rule as the on-target CLI. */
        if (asm_default_output(argv[1], output_name) != ASM_OK) {
            fprintf(stderr, "source name is not 8.3: %s\n", argv[1]);
            return 1;
        }
        destination = output_name;
    } else {
        fprintf(stderr, "usage: asm-host SRC [DST]\n");
        return 2;
    }
    asm_ctx_init(&ctx);
    asm_set_warn(&ctx, warn_stderr, (void*)argv[1]);
    for (pass = 1; pass <= 2; pass++) {
        asm_assemble_begin(&ctx, pass);
        if (run_source(&ctx, argv[1]) != 0)
            return 1;
    }
    out = fopen(destination, "wb");
    if (!out) {
        fprintf(stderr, "%s: cannot write\n", destination);
        return 1;
    }
    if (asm_obj_write(&ctx, file_sink, out) != ASM_OK) {
        fprintf(stderr, "%s: object write failed\n", destination);
        fclose(out);
        return 1;
    }
    fclose(out);
    printf("%s: text %u data %u bss %u symbols %u relocs %u\n", destination,
           ctx.offset[ASM_SECTION_TEXT], ctx.offset[ASM_SECTION_DATA],
           ctx.bss_size, ctx.symbol_count, ctx.reloc_count);
    return 0;
}
