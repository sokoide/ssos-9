/* Host regression tests for the linker capacity checks: crafted S9O1
 * headers that wrap the uint32 size arithmetic must fail with
 * LINK_ERR_LIMIT before any symbol/pool write (F-01), lk_image_plan must
 * state the final-size bound explicitly (R-10), and the image CRC32 must
 * match an independent bit-serial reference (R-13). All inputs are built
 * in memory and served through a LinkIo shim, so a crafted header never
 * needs a matching file body. Compile like tests/Makefile's link-host:
 *   cc -std=c11 -Wall -Wextra -Werror -I../apps/link unit/test_link_wrap.c \
 *       ../apps/link/readobj.c ../apps/link/layout.c ../apps/link/fold.c \
 *       ../apps/link/image.c ../apps/support.c -o build/test-link-wrap
 */

#include "link.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        checks++;                                                  \
        if (!(cond)) {                                             \
            failures++;                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

/* --- in-memory input files served through the LinkIo shim --- */

#define TEST_FILE_MAX 4
#define TEST_BODY_MAX 16384

typedef struct {
    const char* name;
    uint8_t body[TEST_BODY_MAX];
    uint32_t size;
    uint32_t position;
    int open;
} TestFile;

static TestFile test_files[TEST_FILE_MAX];
static LinkLayout layout;
static LinkImage image;

/* Called at the start of every test case: each case registers at most
 * two files, so slots never run out. */
static void files_clear(void) {
    uint32_t i;
    for (i = 0; i < TEST_FILE_MAX; i++) {
        TestFile* f = &test_files[i];
        f->name = NULL;
        f->size = 0;
        f->position = 0;
        f->open = 0;
    }
}

static TestFile* file_reset(const char* name) {
    uint32_t i;
    for (i = 0; i < TEST_FILE_MAX; i++) {
        TestFile* f = &test_files[i];
        if (f->name == NULL || strcmp(f->name, name) == 0) {
            f->name = name;
            f->size = 0;
            f->position = 0;
            f->open = 0;
            return f;
        }
    }
    return NULL;
}

static void* io_open(const char* path) {
    uint32_t i;
    for (i = 0; i < TEST_FILE_MAX; i++) {
        TestFile* f = &test_files[i];
        if (f->name != NULL && strcmp(f->name, path) == 0) {
            f->position = 0;
            f->open = 1;
            return f;
        }
    }
    return NULL;
}

static int32_t io_read(void* handle, void* buffer, uint32_t length) {
    TestFile* f = (TestFile*)handle;
    uint32_t remain;
    uint32_t take;
    if (!f->open)
        return -1;
    remain = f->size - f->position;
    take = length < remain ? length : remain;
    memcpy(buffer, f->body + f->position, take);
    f->position += take;
    return (int32_t)take;
}

static int32_t io_close(void* handle) {
    ((TestFile*)handle)->open = 0;
    return 0;
}

/* --- S9O1 body builder (big-endian, layout per readobj.c) --- */

static void b8(TestFile* f, uint32_t value) {
    f->body[f->size++] = (uint8_t)value;
}

static void b16(TestFile* f, uint32_t value) {
    b8(f, value >> 8);
    b8(f, value);
}

static void b32(TestFile* f, uint32_t value) {
    b16(f, value >> 16);
    b16(f, value & 0xFFFFu);
}

/* The 40-byte header: magic, version 1, flags 0, sizes, entry 0,
 * reserved 0 (field order and widths per lk_obj_header). */
static void b_header(TestFile* f, uint32_t text, uint32_t data, uint32_t bss,
                     uint32_t symbols, uint32_t relocs, uint32_t pool) {
    b8(f, 'S');
    b8(f, '9');
    b8(f, 'O');
    b8(f, '1');
    b16(f, 1); /* version */
    b16(f, 0); /* flags */
    b32(f, text);
    b32(f, data);
    b32(f, bss);
    b32(f, 0); /* entry */
    b32(f, symbols);
    b32(f, relocs);
    b32(f, pool);
    b32(f, 0); /* reserved */
}

/* One ABS local named by the pool's first (NUL) byte. */
static void b_symbol(TestFile* f) {
    b32(f, 0); /* name offset */
    b8(f, LINK_SECTION_ABS);
    b8(f, 0); /* flags: local */
    b16(f, 0); /* reserved */
    b32(f, 0); /* value */
}

/* Valid object: 4 text bytes, `symbols` ABS locals, `pool` zero bytes. */
static void make_object(const char* name, uint32_t symbols, uint32_t pool) {
    TestFile* f = file_reset(name);
    uint32_t i;
    b_header(f, 4, 0, 0, symbols, 0, pool);
    b32(f, 0x4E714E71u); /* two NOPs */
    for (i = 0; i < symbols; i++)
        b_symbol(f);
    for (i = 0; i < pool; i++)
        b8(f, 0);
}

/* Header-only object: every wrap and cumulative-limit case here is
 * rejected before the first body read, so no body is needed. */
static void make_crafted(const char* name, uint32_t text, uint32_t data,
                         uint32_t bss, uint32_t symbols, uint32_t relocs,
                         uint32_t pool) {
    TestFile* f = file_reset(name);
    b_header(f, text, data, bss, symbols, relocs, pool);
}

/* --- drivers mirroring apps/link/main.c --- */

static LinkStatus run_layout(const char* in0, const char* in1) {
    LinkIo io;
    LinkStatus status;
    io.open_read = io_open;
    io.read = io_read;
    io.close = io_close;
    lk_layout_init(&layout, in1 != NULL ? 2u : 1u);
    status = lk_layout_input(&layout, &io, in0, 0);
    if (status == LINK_OK && in1 != NULL)
        status = lk_layout_input(&layout, &io, in1, 1);
    return status;
}

static LinkStatus run_link(const char* in0, const char* in1) {
    LinkIo io;
    LinkStatus status;
    io.open_read = io_open;
    io.read = io_read;
    io.close = io_close;
    lk_image_init(&image);
    lk_layout_init(&layout, 2);
    status = lk_layout_input(&layout, &io, in0, 0);
    if (status == LINK_OK)
        status = lk_layout_input(&layout, &io, in1, 1);
    if (status == LINK_OK)
        status = lk_layout_resolve(&layout);
    if (status == LINK_OK)
        status = lk_image_plan(&image, &layout, 7);
    if (status == LINK_OK) {
        status = lk_fold_input(&image, &layout, &io, in0, 0);
        if (status == LINK_OK)
            status = lk_fold_input(&image, &layout, &io, in1, 1);
    }
    if (status == LINK_OK)
        status = lk_image_finish(&image, "wrapmod", 7);
    return status;
}

/* --- F-01: crafted headers must not wrap past the capacity checks --- */

static void test_crafted_headers(void) {
    /* Single crafted input: symbol_count = 0xFFFFFFFF. */
    files_clear();
    make_crafted("symwrap", 0, 0, 0, 0xFFFFFFFFu, 0, 0);
    CHECK(run_layout("symwrap", NULL) == LINK_ERR_LIMIT);
    CHECK(layout.symbol_count == 0 && layout.pool_size == 0);

    /* Second input crafted with 1..2 symbols already committed: the
     * old additive check computed 2 + 0xFFFFFFFF == 1 and passed. */
    files_clear();
    make_object("two", 2, 1);
    make_crafted("symwrap2", 0, 0, 0, 0xFFFFFFFFu, 0, 0);
    CHECK(run_layout("two", "symwrap2") == LINK_ERR_LIMIT);
    CHECK(layout.symbol_count == 2 && layout.pool_size == 1);

    /* pool_size = 0xFFFFFFFF, single and second input. */
    files_clear();
    make_crafted("poolwrap", 0, 0, 0, 0, 0, 0xFFFFFFFFu);
    CHECK(run_layout("poolwrap", NULL) == LINK_ERR_LIMIT);
    CHECK(layout.pool_size == 0);

    files_clear();
    make_object("min", 0, 0);
    make_crafted("poolwrap2", 0, 0, 0, 0, 0, 0xFFFFFFFFu);
    CHECK(run_layout("min", "poolwrap2") == LINK_ERR_LIMIT);
    CHECK(layout.pool_size == 0);

    /* Every other header field wraps the same way; each cap rejects. */
    files_clear();
    make_crafted("textwrap", 0xFFFFFFFFu, 0, 0, 0, 0, 0);
    CHECK(run_layout("textwrap", NULL) == LINK_ERR_LIMIT);
    files_clear();
    make_crafted("datawrap", 0, 0xFFFFFFFFu, 0, 0, 0, 0);
    CHECK(run_layout("datawrap", NULL) == LINK_ERR_LIMIT);
    files_clear();
    make_crafted("bsswrap", 0, 0, 0xFFFFFFFFu, 0, 0, 0);
    CHECK(run_layout("bsswrap", NULL) == LINK_ERR_LIMIT);
    files_clear();
    make_crafted("relocwrap", 0, 0, 0, 0, 0xFFFFFFFFu, 0);
    CHECK(run_layout("relocwrap", NULL) == LINK_ERR_LIMIT);

    /* Crafted header backed by a real oversized body: the wrapped
     * checks admitted these and the scan wrote 520 symbols into the
     * 512-entry table and 6300 pool bytes into the 6144-byte pool. The
     * caps reject the header before any body read. */
    files_clear();
    make_object("two", 2, 1);
    {
        TestFile* f = file_reset("bigbody");
        uint32_t i;
        b_header(f, 4, 0, 0, 0xFFFFFFFFu, 0, 0);
        b32(f, 0x4E714E71u);
        for (i = 0; i < 520; i++)
            b_symbol(f);
    }
    CHECK(run_layout("two", "bigbody") == LINK_ERR_LIMIT);
    CHECK(layout.symbol_count == 2 && layout.pool_size == 1);

    files_clear();
    make_object("min", 0, 0);
    {
        TestFile* f = file_reset("bigpool");
        uint32_t i;
        b_header(f, 0, 0, 0, 0, 0, 0xFFFFFFFFu);
        for (i = 0; i < 6300; i++)
            b8(f, 0);
    }
    CHECK(run_layout("min", "bigpool") == LINK_ERR_LIMIT);
    CHECK(layout.pool_size == 0);
}

/* Cumulative over-limit inputs: each header passes lk_obj_header, so
 * only the subtractive cumulative check in layout.c may reject. */

static void test_cumulative_limits(void) {
    files_clear();
    make_object("a300", 300, 1);
    make_crafted("b300", 4, 0, 0, 300, 0, 1);
    CHECK(run_layout("a300", "b300") == LINK_ERR_LIMIT);
    CHECK(layout.symbol_count == 300); /* no growth past the first input */

    files_clear();
    make_object("p5000", 0, 5000);
    make_crafted("p2000", 0, 0, 0, 0, 0, 2000);
    CHECK(run_layout("p5000", "p2000") == LINK_ERR_LIMIT);
    CHECK(layout.pool_size == 5000);

    /* Exactly at both limits must still link. */
    files_clear();
    make_object("half0", 256, 3072);
    make_object("half1", 256, 3072);
    CHECK(run_layout("half0", "half1") == LINK_OK);
    CHECK(layout.symbol_count == LINK_SYMBOL_MAX);
    CHECK(layout.pool_size == LINK_NAME_POOL_MAX);
}

/* Full pipeline at the symbol/pool limits; the image CRC field must
 * match an independent bit-serial IEEE CRC32 reference. */

static uint32_t crc32_reference(uint32_t crc, const uint8_t* data,
                                uint32_t length) {
    uint32_t i;
    int bit;
    for (i = 0; i < length; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static uint32_t image_crc_field(const LinkImage* img) {
    return ((uint32_t)img->bytes[0x34] << 24) |
           ((uint32_t)img->bytes[0x35] << 16) |
           ((uint32_t)img->bytes[0x36] << 8) | img->bytes[0x37];
}

static void test_boundary_link(void) {
    files_clear();
    make_object("half0", 256, 3072);
    make_object("half1", 256, 3072);
    CHECK(run_link("half0", "half1") == LINK_OK);
    CHECK(layout.symbol_count == LINK_SYMBOL_MAX);
    CHECK(layout.pool_size == LINK_NAME_POOL_MAX);
    /* header + code(4+4) + init(dummy GOT 4) + name, no fixups. */
    CHECK(image.image_size == 80u + 8u + 4u + 7u);
    {
        /* lk_image_finish computes the CRC with the field zeroed, so the
         * reference must re-run the same way. */
        uint32_t stored = image_crc_field(&image);
        memset(image.bytes + 0x34, 0, 4);
        CHECK(stored ==
              (crc32_reference(0xFFFFFFFFu, image.bytes, image.image_size) ^
               0xFFFFFFFFu));
    }
}

/* R-10: lk_image_plan must state the final-size bound explicitly, not
 * rely on pass-1 caps happening to sum below the buffer. The layouts
 * below are synthetic (pass 1 never commits data_total over
 * LINK_DATA_MAX), which is exactly the point: plan enforces its own
 * bounds even when its caller was not layout. */

static void test_plan_bounds(void) {
    lk_layout_init(&layout, 1);

    /* init data over LINK_DATA_MAX: rejected by the explicit cap. */
    layout.text_total = 32700;
    layout.data_total = 20000;
    lk_image_init(&image);
    CHECK(lk_image_plan(&image, &layout, 7) == LINK_ERR_LIMIT);

    /* init_size = 0xFFFFF000 wraps the additive data_offset sum
     * (80 + 2000 + 0xFFFFF000 == 1056 mod 2^32) into the buffer; the
     * explicit init-data cap rejects it before any offset is used. */
    layout.text_total = 2000;
    layout.data_total = 0xFFFFF000u;
    lk_image_init(&image);
    CHECK(lk_image_plan(&image, &layout, 7) == LINK_ERR_LIMIT);

    /* Passes the data_offset sum, but the worst-case fixup table
     * (12 * 256) no longer fits the buffer. */
    layout.text_total = LINK_TEXT_MAX;
    layout.data_total = 12320;
    lk_image_init(&image);
    CHECK(lk_image_plan(&image, &layout, 7) == LINK_ERR_LIMIT);

    /* Name over the module-name rule. */
    layout.text_total = 100;
    layout.data_total = 0;
    lk_image_init(&image);
    CHECK(lk_image_plan(&image, &layout, LINK_MODULE_NAME_MAX + 1u) ==
          LINK_ERR_LIMIT);

    /* Worst legal geometry: full code, full init data, longest name. */
    layout.text_total = LINK_TEXT_MAX;
    layout.data_total = LINK_DATA_MAX;
    lk_image_init(&image);
    CHECK(lk_image_plan(&image, &layout, LINK_MODULE_NAME_MAX) == LINK_OK);
    CHECK(image.fixup_offset + 12u * LINK_FIXUP_MAX ==
          LINK_IMAGE_BUF_MAX - 1u);
}

int main(void) {
    test_crafted_headers();
    test_cumulative_limits();
    test_boundary_link();
    test_plan_bounds();
    printf("link wrap tests: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
