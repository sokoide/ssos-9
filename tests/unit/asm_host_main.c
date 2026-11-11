/* Host driver for the assembler core: same two-pass pipeline as the S9M1
 * module, with stdio file access. Used by unit and golden tests. */

#include "asm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int file_sink(const void* bytes, uint32_t length, void* user) {
    return fwrite(bytes, 1, length, (FILE*)user) == length ? 0 : 1;
}

static int run_source(AsmCtx* ctx, const char* path) {
    FILE* file = fopen(path, "r");
    char line[512];
    if (!file) {
        fprintf(stderr, "%s: cannot open\n", path);
        return -1;
    }
    while (fgets(line, sizeof(line), file)) {
        size_t n = strlen(line);
        while (n && (line[n - 1u] == '\n' || line[n - 1u] == '\r'))
            line[--n] = 0;
        /* Same limit as the on-target reader: lines of 200 bytes or more
         * are rejected so escape-resolved strings cannot exceed the
         * statement storage (ASM_LINE_MAX). */
        if (n >= ASM_LINE_MAX) {
            fprintf(stderr, "%s:%u: line too long\n", path, ctx->line + 1u);
            fclose(file);
            return -1;
        }
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
