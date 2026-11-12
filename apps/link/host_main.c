/* Host driver for the linker core: same CLI flow as the S9M1 module in
 * main.c, with stdio file access. Build with `make -C apps/link host`.
 * Used for manual end-to-end verification; the target build does not
 * include this file. */

#include "link.h"

#include <stdio.h>
#include <string.h>

static void* io_open(const char* path) { return fopen(path, "rb"); }

static int32_t io_read(void* handle, void* buffer, uint32_t length) {
    size_t n = fread(buffer, 1, length, (FILE*)handle);
    if (n == 0 && length != 0 && ferror((FILE*)handle))
        return -1;
    return (int32_t)n;
}

static int32_t io_close(void* handle) {
    return fclose((FILE*)handle) == 0 ? 0 : -1;
}

static void report(const char* file, LinkStatus status,
                   const LinkLayout* layout) {
    char message[128];
    (void)lk_error_message(status, &layout->error, layout, message,
                           sizeof(message));
    if (file != NULL)
        printf("%s: %s\n", file, message);
    else
        printf("ld: %s\n", message);
}

static int name_ci_equal(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
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

int main(int argc, char** argv) {
    static LinkImage image; /* host driver: no stack budget */
    static LinkLayout layout;
    LinkIo file_io;
    char module_name[LINK_MODULE_NAME_MAX + 1u];
    LinkStatus status;
    uint32_t inputs;
    uint32_t i;
    FILE* out;

    file_io.open_read = io_open;
    file_io.read = io_read;
    file_io.close = io_close;

    if (argc < 3) {
        printf("Usage: link-host OUTPUT IN1 [IN2 ...]\n");
        return 2;
    }
    inputs = (uint32_t)(argc - 2);
    if (inputs > LINK_INPUT_MAX) {
        printf("ld: Too many input objects\n");
        return 1;
    }
    if (lk_module_name(argv[1], module_name) != LINK_OK) {
        printf("ld: Invalid module name: %s\n", argv[1]);
        return 2;
    }
    for (i = 0; i < inputs; i++) {
        if (name_ci_equal(argv[1], argv[2 + i])) {
            printf("ld: Output name matches an input name\n");
            return 2;
        }
    }

    lk_image_init(&image);
    lk_layout_init(&layout, inputs);
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
    status = lk_image_plan(&image, &layout, (uint32_t)strlen(module_name));
    if (status != LINK_OK) {
        report(NULL, status, &layout);
        return 1;
    }
    for (i = 0; i < inputs; i++) {
        status = lk_fold_input(&image, &layout, &file_io, argv[2 + i], i);
        if (status != LINK_OK) {
            report(argv[2 + i], status, &layout);
            return 1;
        }
    }
    status =
        lk_image_finish(&image, module_name, (uint32_t)strlen(module_name));
    if (status != LINK_OK) {
        report(NULL, status, &layout);
        return 1;
    }

    out = fopen(argv[1], "wb");
    if (!out) {
        printf("%s: Cannot write output\n", argv[1]);
        return 1;
    }
    if (fwrite(image.bytes, 1, image.image_size, out) != image.image_size) {
        printf("%s: Output write failed\n", argv[1]);
        fclose(out);
        return 1;
    }
    fclose(out);
    printf("Wrote %s: %u bytes, text %u, data %u, bss %u, fixups %u\n", argv[1],
           image.image_size, image.text_size, image.data_size, image.bss_size,
           image.fixup_count);
    return 0;
}
