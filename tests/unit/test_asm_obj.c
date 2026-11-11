#include "../../apps/asm/asm.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t bytes[70000];
    uint32_t length;
    int fail_on; /* 0 = never fail; N = fail on the Nth call */
    int calls;
} MemSink;

static int mem_sink(const void* bytes, uint32_t length, void* user) {
    MemSink* sink = (MemSink*)user;
    sink->calls++;
    if (sink->fail_on != 0 && sink->calls == sink->fail_on)
        return 1;
    assert(sink->length + length <= sizeof(sink->bytes));
    memcpy(sink->bytes + sink->length, bytes, length);
    sink->length += length;
    return 0;
}

static uint32_t be16_at(const uint8_t* p) {
    return ((uint32_t)p[0] << 8) | p[1];
}

static uint32_t be32_at(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void set_name(AsmSymbol* symbol, const char* name) {
    size_t i;
    for (i = 0; name[i]; i++) symbol->name[i] = name[i];
    symbol->name[i] = 0;
}

/* text: nop; rts; then a zeroed .w and .l field for relocations. */
static const uint8_t sample_text[8] = {0x4E, 0x71, 0x4E, 0x75,
                                       0x00, 0x00, 0x00, 0x00};
static const uint8_t sample_data[4] = {'A', 'B', 0x00, 0x01};

static void build_sample(AsmCtx* ctx) {
    memset(ctx, 0, sizeof(*ctx));
    memcpy(ctx->text, sample_text, sizeof(sample_text));
    ctx->offset[ASM_SECTION_TEXT] = sizeof(sample_text);
    memcpy(ctx->data, sample_data, sizeof(sample_data));
    ctx->offset[ASM_SECTION_DATA] = sizeof(sample_data);
    ctx->bss_size = 8;

    set_name(&ctx->symbols[0], "_start");
    ctx->symbols[0].section = ASM_SECTION_TEXT;
    ctx->symbols[0].flags = ASM_SYM_GLOBAL;
    ctx->symbols[0].value = 0;
    set_name(&ctx->symbols[1], "loop");
    ctx->symbols[1].section = ASM_SECTION_TEXT;
    ctx->symbols[1].flags = 0;
    ctx->symbols[1].value = 2;
    set_name(&ctx->symbols[2], "msg");
    ctx->symbols[2].section = ASM_SECTION_DATA;
    ctx->symbols[2].flags = ASM_SYM_GLOBAL;
    ctx->symbols[2].value = 0;
    set_name(&ctx->symbols[3], "count");
    ctx->symbols[3].section = ASM_SECTION_ABS;
    ctx->symbols[3].flags = ASM_SYM_GLOBAL;
    ctx->symbols[3].value = 100;
    set_name(&ctx->symbols[4], "ext_func");
    ctx->symbols[4].section = ASM_SECTION_UNDEF;
    ctx->symbols[4].flags = ASM_SYM_EXTERN;
    ctx->symbols[4].value = 0;
    ctx->symbol_count = 5;

    ctx->relocs[0].section = ASM_SECTION_TEXT;
    ctx->relocs[0].type = ASM_RELOC_PC16;
    ctx->relocs[0].offset = 4;
    ctx->relocs[0].symbol = 1;
    ctx->relocs[0].addend = -2;
    ctx->relocs[1].section = ASM_SECTION_TEXT;
    ctx->relocs[1].type = ASM_RELOC_ABS32;
    ctx->relocs[1].offset = 0;
    ctx->relocs[1].symbol = 4;
    ctx->relocs[1].addend = 0x100;
    ctx->relocs[2].section = ASM_SECTION_DATA;
    ctx->relocs[2].type = ASM_RELOC_ABS16;
    ctx->relocs[2].offset = 2;
    ctx->relocs[2].symbol = 3;
    ctx->relocs[2].addend = -1;
    ctx->relocs[3].section = ASM_SECTION_DATA;
    ctx->relocs[3].type = ASM_RELOC_ABS8;
    ctx->relocs[3].offset = 0;
    ctx->relocs[3].symbol = 0;
    ctx->relocs[3].addend = 5;
    ctx->reloc_count = 4;
}

static void test_sample_object(void) {
    static const uint8_t header[ASM_OBJ_HEADER_SIZE] = {
        'S',  '9',  'O',  '1',  0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x08, /* text */
        0x00, 0x00, 0x00, 0x04,             /* data */
        0x00, 0x00, 0x00, 0x08,             /* bss */
        0x00, 0x00, 0x00, 0x00,             /* entry */
        0x00, 0x00, 0x00, 0x05,             /* symbols */
        0x00, 0x00, 0x00, 0x04,             /* relocs */
        0x00, 0x00, 0x00, 0x1F,             /* name pool: 7+5+4+6+9 */
        0x00, 0x00, 0x00, 0x00,             /* reserved */
    };
    static const uint32_t name_offsets[5] = {0, 7, 12, 16, 22};
    static const char pool[31] = "_start\0loop\0msg\0count\0ext_func\0";
    static const uint8_t sections[5] = {ASM_SECTION_TEXT, ASM_SECTION_TEXT,
                                        ASM_SECTION_DATA, ASM_SECTION_ABS,
                                        ASM_SECTION_UNDEF};
    static const uint8_t sym_flags[5] = {ASM_SYM_GLOBAL, 0, ASM_SYM_GLOBAL,
                                         ASM_SYM_GLOBAL, ASM_SYM_EXTERN};
    static const uint32_t values[5] = {0, 2, 0, 100, 0};
    static const uint32_t reloc_fields[4][3] = {
        {4, 1, (uint32_t)(int32_t)-2},
        {0, 4, 0x100},
        {2, 3, (uint32_t)(int32_t)-1},
        {0, 0, 5},
    };
    static const uint8_t reloc_section[4] = {
        ASM_SECTION_TEXT, ASM_SECTION_TEXT, ASM_SECTION_DATA, ASM_SECTION_DATA};
    static const uint8_t reloc_type[4] = {ASM_RELOC_PC16, ASM_RELOC_ABS32,
                                          ASM_RELOC_ABS16, ASM_RELOC_ABS8};
    const uint32_t text_at = ASM_OBJ_HEADER_SIZE;
    const uint32_t data_at = text_at + 8;
    const uint32_t sym_at = data_at + 4;
    const uint32_t reloc_at = sym_at + 5 * ASM_OBJ_SYMBOL_SIZE;
    const uint32_t pool_at = reloc_at + 4 * ASM_OBJ_RELOC_SIZE;
    AsmCtx ctx;
    MemSink sink;
    uint32_t i;

    build_sample(&ctx);
    memset(&sink, 0, sizeof(sink));
    assert(asm_obj_write(&ctx, mem_sink, &sink) == ASM_OK);
    assert(sink.length == pool_at + 31);
    assert(memcmp(sink.bytes, header, sizeof(header)) == 0);
    assert(memcmp(sink.bytes + text_at, sample_text, sizeof(sample_text)) == 0);
    assert(memcmp(sink.bytes + data_at, sample_data, sizeof(sample_data)) == 0);

    for (i = 0; i < 5; i++) {
        const uint8_t* rec = sink.bytes + sym_at + i * ASM_OBJ_SYMBOL_SIZE;
        assert(be32_at(rec) == name_offsets[i]);
        assert(rec[4] == sections[i]);
        assert(rec[5] == sym_flags[i]);
        assert(be16_at(rec + 6) == 0);
        assert(be32_at(rec + 8) == values[i]);
        /* the recorded offset points at the right NUL-terminated name */
        assert(strcmp((const char*)sink.bytes + pool_at + name_offsets[i],
                      (const char*)pool + name_offsets[i]) == 0);
    }
    assert(memcmp(sink.bytes + pool_at, pool, sizeof(pool)) == 0);

    for (i = 0; i < 4; i++) {
        const uint8_t* rec = sink.bytes + reloc_at + i * ASM_OBJ_RELOC_SIZE;
        assert(be32_at(rec) == reloc_fields[i][0]);
        assert(be32_at(rec + 4) == reloc_fields[i][1]);
        assert(be32_at(rec + 8) == reloc_fields[i][2]);
        assert(rec[12] == reloc_section[i]);
        assert(rec[13] == reloc_type[i]);
        assert(be16_at(rec + 14) == 0);
    }
}

static void test_empty_object(void) {
    AsmCtx ctx;
    MemSink sink;

    memset(&ctx, 0, sizeof(ctx));
    memset(&sink, 0, sizeof(sink));
    assert(asm_obj_write(&ctx, mem_sink, &sink) == ASM_OK);
    assert(sink.length == ASM_OBJ_HEADER_SIZE);
    assert(memcmp(sink.bytes, "S9O1", 4) == 0);
    assert(be32_at(sink.bytes + 8) == 0 && be32_at(sink.bytes + 12) == 0);
    assert(be32_at(sink.bytes + 24) == 0 && be32_at(sink.bytes + 28) == 0);
    assert(be32_at(sink.bytes + 32) == 0);
    /* zero-size text/data emit no sink calls: header only */
    assert(sink.calls == 1);
}

static void test_size_limit(void) {
    /* over-allocate so the boundary-size text stream stays in bounds */
    AsmCtx* big = calloc(1, sizeof(AsmCtx) + 70000);
    MemSink sink;

    assert(big != NULL);
    big->offset[ASM_SECTION_TEXT] = 65495; /* 40 + 65495 == 65535: allowed */
    memset(&sink, 0, sizeof(sink));
    assert(asm_obj_write(big, mem_sink, &sink) == ASM_OK);
    assert(sink.length == 65535);

    big->offset[ASM_SECTION_TEXT] =
        65496; /* 65536: rejected before any write */
    memset(&sink, 0, sizeof(sink));
    assert(asm_obj_write(big, mem_sink, &sink) == ASM_ERR_RANGE);
    assert(sink.calls == 0 && sink.length == 0);

    big->offset[ASM_SECTION_TEXT] = 0;
    big->bss_size = 65496; /* bss alone must not trip the object size check */
    memset(&sink, 0, sizeof(sink));
    assert(asm_obj_write(big, mem_sink, &sink) == ASM_OK);
    assert(sink.length == ASM_OBJ_HEADER_SIZE);
    free(big);
}

static void test_sink_failure(void) {
    AsmCtx ctx;
    MemSink sink;
    int n;

    build_sample(&ctx);
    /* header + text + data + 5 symbols + 4 relocs + 10 pool chunks */
    for (n = 1; n <= 22; n++) {
        memset(&sink, 0, sizeof(sink));
        sink.fail_on = n;
        assert(asm_obj_write(&ctx, mem_sink, &sink) == ASM_ERR_WRITE);
        assert(sink.calls == n);
    }
    memset(&sink, 0, sizeof(sink));
    assert(asm_obj_write(&ctx, mem_sink, &sink) == ASM_OK);
    assert(sink.calls == 22);
}

int main(void) {
    test_sample_object();
    test_empty_object();
    test_size_limit();
    test_sink_failure();
    return 0;
}
