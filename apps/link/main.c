/* S9M1 linker module CLI: run ld OUTPUT IN1 [IN2 ...].
 *
 * Everything lives on this stack frame (the S9M1 code-only profile
 * forbids static data; module stack 65532). Inputs are read twice
 * (pass 1 scan, pass 2 fold) via sequential reads; the finished image
 * leaves in one ss9_file_store so no partial output file can exist. */

#include "link.h"
#include "ssos9/user.h"

#include <stddef.h>

static void* io_open(const char* path) {
    int32_t fd = ss9_open_read(path);
    return fd < 0 ? NULL : (void*)(uintptr_t)fd;
}

static int32_t io_read(void* handle, void* buffer, uint32_t length) {
    return ss9_read((uint32_t)(uintptr_t)handle, buffer, length);
}

static int32_t io_close(void* handle) {
    return ss9_close((uint32_t)(uintptr_t)handle);
}

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

/* Prints "ld: message" or, with a file name, "FILE: message". */
static void report(const char* file, LinkStatus status,
                   const LinkLayout* layout) {
    char message[96];
    (void)lk_error_message(status, &layout->error, layout, message,
                           sizeof(message));
    if (file != NULL) {
        (void)write_text(file);
        (void)write_text(": ");
    } else {
        (void)write_text("ld: ");
    }
    (void)write_text(message);
    (void)write_text("\r\n");
}

/* Case-insensitive 8.3 name comparison for the output/input clash check. */
static int name_ci_equal(const char* a, const char* b) {
    while (*a != 0 && *b != 0) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'a' && ca <= 'z')
            ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z')
            cb = (char)(cb - 'a' + 'A');
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

/* Storage paths may include directories; the module name rule applies to
 * the base name, matching the host driver. */
static const char* base_name(const char* path) {
    const char* base = path;
    const char* p;
    for (p = path; *p != 0; p++) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }
    return base;
}

int main(int argc, char** argv) {
    LinkImage image;
    LinkLayout layout;
    LinkIo file_io;
    char module_name[LINK_MODULE_NAME_MAX + 1u];
    LinkStatus status;
    uint32_t inputs;
    uint32_t i;
    int32_t existing;
    int32_t written;

    file_io.open_read = io_open;
    file_io.read = io_read;
    file_io.close = io_close;

    if (argc < 3) {
        (void)write_text("Usage: run ld OUTPUT IN1 [IN2 ...]\r\n");
        return 2;
    }
    inputs = (uint32_t)(argc - 2);
    if (inputs > LINK_INPUT_MAX) {
        (void)write_text("ld: Too many input objects\r\n");
        return 1;
    }

    if (lk_module_name(base_name(argv[1]), module_name) != LINK_OK) {
        (void)write_text("ld: Invalid module name: ");
        (void)write_text(argv[1]);
        (void)write_text("\r\n");
        return 2;
    }
    for (i = 0; i < inputs; i++) {
        if (name_ci_equal(argv[1], argv[2 + i])) {
            (void)write_text("ld: Output name matches an input name\r\n");
            return 2;
        }
    }

    lk_image_init(&image);
    lk_layout_init(&layout, inputs);

    /* Pass 1: scan inputs, build symbols and section bases. */
    for (i = 0; i < inputs; i++) {
        status = lk_layout_input(&layout, &file_io, argv[2 + i], i);
        if (status != LINK_OK) {
            report(argv[2 + i], status, &layout);
            return 1;
        }
    }

    status = lk_layout_resolve(&layout);
    if (status != LINK_OK) {
        report(NULL, status, &layout);
        return 1;
    }
    status = lk_image_plan(&image, &layout, text_length(module_name));
    if (status != LINK_OK) {
        report(NULL, status, &layout);
        return 1;
    }

    /* Pass 2: fold sections and relocations into the image. */
    for (i = 0; i < inputs; i++) {
        status = lk_fold_input(&image, &layout, &file_io, argv[2 + i], i);
        if (status != LINK_OK) {
            report(argv[2 + i], status, &layout);
            return 1;
        }
    }

    status = lk_image_finish(&image, module_name, text_length(module_name));
    if (status != LINK_OK) {
        report(NULL, status, &layout);
        return 1;
    }

    /* One atomic store; probe first for replace-vs-create like asm. */
    existing = ss9_open_read(argv[1]);
    if (existing >= 0)
        (void)ss9_close((uint32_t)existing);
    written =
        ss9_file_store(argv[1], image.bytes, image.image_size,
                       existing >= 0 ? SS9_FILE_REPLACE : SS9_FILE_CREATE);
    if (written != (int32_t)image.image_size) {
        (void)write_text(argv[1]);
        (void)write_text(": Cannot write output\r\n");
        return 1;
    }

    (void)write_text("Wrote ");
    (void)write_text(argv[1]);
    (void)write_text(": ");
    (void)write_number(image.image_size);
    (void)write_text(" bytes, text ");
    (void)write_number(image.text_size);
    (void)write_text(", data ");
    (void)write_number(image.data_size);
    (void)write_text(", bss ");
    (void)write_number(image.bss_size);
    (void)write_text(", fixups ");
    (void)write_number(image.fixup_count);
    (void)write_text("\r\n");
    return 0;
}
