/* S9M1 assembler module CLI: run asm SRC [DST] */

#include "asm.h"
#include "ssos9/user.h"

typedef struct {
    int32_t fd;
    uint32_t written;
} ObjSinkUser;

typedef struct {
    int32_t fd;
    uint8_t buffer[256];
    uint32_t fill;
    uint32_t position;
} LineReader;

static uint32_t text_length(const char* text) {
    uint32_t n = 0;
    while (text[n]) n++;
    return n;
}

static int write_text(const char* text) {
    uint32_t n = text_length(text);
    return ss9_write(1, text, n) == (int32_t)n ? 0 : -1;
}

static int write_number(uint32_t value) {
    char digits[10];
    uint32_t n = 0;
    uint32_t i;
    if (value == 0)
        digits[n++] = '0';
    while (value) {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    for (i = n; i > 0; i--) {
        char c = digits[i - 1u];
        if (ss9_write(1, &c, 1) != 1)
            return -1;
    }
    return 0;
}

/* Writes sink data in SS9_WRITE_MAX chunks. */
static int obj_sink(const void* bytes, uint32_t length, void* user) {
    ObjSinkUser* sink = (ObjSinkUser*)user;
    const uint8_t* at = (const uint8_t*)bytes;
    while (length) {
        uint32_t n = length > SS9_WRITE_MAX ? SS9_WRITE_MAX : length;
        if (ss9_write((uint32_t)sink->fd, at, n) != (int32_t)n)
            return 1;
        sink->written += n;
        at += n;
        length -= n;
    }
    return 0;
}

/* Reads one line into out (NUL-terminated, CR/LF stripped).
 * Returns 1 on a line, 0 on EOF, -1 on read error or line too long. */
static int reader_line(LineReader* reader, char* out) {
    uint32_t length = 0;
    for (;;) {
        uint8_t c;
        if (reader->position >= reader->fill) {
            int32_t n = ss9_read((uint32_t)reader->fd, reader->buffer,
                                 sizeof(reader->buffer));
            if (n < 0)
                return -1;
            if (n == 0) {
                if (length == 0)
                    return 0;
                out[length] = 0;
                return 1;
            }
            reader->fill = (uint32_t)n;
            reader->position = 0;
        }
        c = reader->buffer[reader->position++];
        if (c == '\n') {
            out[length] = 0;
            return 1;
        }
        if (c == '\r')
            continue;
        if (length >= ASM_LINE_MAX)
            return -1;
        out[length++] = (char)c;
    }
}

static int run_pass(AsmCtx* ctx, const char* source) {
    LineReader reader;
    char line[ASM_LINE_MAX + 1u];
    int32_t fd = ss9_open_read(source);
    int status;
    if (fd < 0) {
        /* same message as the host driver: "SRC: cannot open" */
        (void)write_text(source);
        (void)write_text(": cannot open\r\n");
        return -1;
    }
    reader.fd = fd;
    reader.fill = 0;
    reader.position = 0;
    for (;;) {
        int got = reader_line(&reader, line);
        if (got < 0) {
            (void)write_text(source);
            (void)write_text(":");
            (void)write_number(ctx->line);
            (void)write_text(": line too long or read error\r\n");
            (void)ss9_close((uint32_t)fd);
            return -1;
        }
        if (got == 0)
            break;
        if (asm_assemble_line(ctx, line) != ASM_OK) {
            (void)write_text(source);
            (void)write_text(":");
            (void)write_number(ctx->error_line);
            (void)write_text(": ");
            (void)write_text(asm_status_message(ctx->status));
            (void)write_text("\r\n");
            (void)ss9_close((uint32_t)fd);
            return -1;
        }
        if (ctx->ended)
            break;
    }
    if (ss9_close((uint32_t)fd) != 0)
        return -1;
    status = asm_assemble_end(ctx) == ASM_OK ? 0 : -1;
    if (status != 0) {
        (void)write_text(source);
        (void)write_text(":");
        (void)write_number(ctx->error_line);
        (void)write_text(": ");
        (void)write_text(asm_status_message(ctx->status));
        (void)write_text("\r\n");
    }
    return status;
}

int main(int argc, char** argv) {
    /* The S9M1 code-only profile forbids static data: everything lives on
     * this stack frame (module stack size 65532). */
    AsmCtx ctx;
    char output_name[16];
    const char* source;
    const char* destination;
    ObjSinkUser sink;
    int32_t existing;
    int32_t fd;
    if (argc < 2 || argc > 3) {
        (void)write_text("Usage: run asm SRC [DST]\r\n");
        return 2;
    }
    source = argv[1];
    if (argc == 3) {
        destination = argv[2];
    } else {
        if (asm_default_output(source, output_name) != ASM_OK) {
            (void)write_text("Source name is not 8.3: ");
            (void)write_text(source);
            (void)write_text("\r\n");
            return 1;
        }
        destination = output_name;
    }
    asm_ctx_init(&ctx);
    asm_assemble_begin(&ctx, 1);
    if (run_pass(&ctx, source) != 0)
        return 1;
    asm_assemble_begin(&ctx, 2);
    if (run_pass(&ctx, source) != 0)
        return 1;
    existing = ss9_open_read(destination);
    if (existing >= 0)
        (void)ss9_close((uint32_t)existing);
    fd = ss9_open(destination,
                  existing >= 0 ? SS9_OPEN_REPLACE : SS9_OPEN_CREATE);
    if (fd < 0) {
        (void)write_text("Cannot write ");
        (void)write_text(destination);
        (void)write_text("\r\n");
        return 1;
    }
    sink.fd = fd;
    sink.written = 0;
    if (asm_obj_write(&ctx, obj_sink, &sink) != ASM_OK ||
        ss9_close((uint32_t)fd) != 0) {
        (void)write_text("Object write failed\r\n");
        return 1;
    }
    (void)write_text("Wrote ");
    (void)write_text(destination);
    (void)write_text(": ");
    (void)write_number(sink.written);
    (void)write_text(" bytes, text ");
    (void)write_number(ctx.offset[ASM_SECTION_TEXT]);
    (void)write_text(", data ");
    (void)write_number(ctx.offset[ASM_SECTION_DATA]);
    (void)write_text(", bss ");
    (void)write_number(ctx.bss_size);
    (void)write_text("\r\n");
    return 0;
}
