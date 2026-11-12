#ifndef SSOS9_APPS_LINK_LINK_H
#define SSOS9_APPS_LINK_LINK_H

/* Shared interfaces for the SSOS-9 linker. See docs/linker-contract.md for
 * the CLI, linking semantics, output format, and diagnostics these types
 * implement. Input objects are Phase 3 S9O1 (docs/assembler-contract.md);
 * output is an S9M1 image loaded by the existing validator/spawn path. */

#include <stdint.h>

/* Contract limits (docs/linker-contract.md, "対象と上限"). */
#define LINK_INPUT_MAX 14
#define LINK_TEXT_MAX 32768u
#define LINK_DATA_MAX 12288u
#define LINK_SYMBOL_MAX 512
#define LINK_PRIVATE_MAX 65536u /* init data + bss ceiling */
#define LINK_NAME_POOL_MAX 6144u
#define LINK_FIXUP_MAX 256
#define LINK_IMAGE_MAX 65536u
#define LINK_MODULE_NAME_MAX 31
#define LINK_STACK_TOTAL 65532u

/* S9O1 object layout. */
#define LINK_OBJ_HEADER_SIZE 40u
#define LINK_OBJ_SYMBOL_SIZE 12u
#define LINK_OBJ_RELOC_SIZE 16u
#define LINK_OBJ_TOTAL_MAX 65535u
#define LINK_READER_CHUNK 256u

/* S9M1 image geometry. */
#define LINK_IMAGE_HEADER_SIZE 80u
#define LINK_IMAGE_BUF_MAX 48240u /* header 80 + code 32768 + data 12288 +
                                     name 32 + fixup 12*256 */

/* S9O1 sections and relocation types. */
#define LINK_SECTION_ABS 0
#define LINK_SECTION_TEXT 1
#define LINK_SECTION_DATA 2
#define LINK_SECTION_BSS 3
#define LINK_SECTION_UNDEF 0xFF

#define LINK_RELOC_PC16 1
#define LINK_RELOC_ABS32 2
#define LINK_RELOC_ABS16 3
#define LINK_RELOC_ABS8 4

#define LINK_FIXUP_CODE32 1u
#define LINK_FIXUP_DATA32 2u

#define LINK_SYM_GLOBAL 0x01u
#define LINK_SYM_EXTERN 0x02u
#define LINK_SYM_SECTION 0x04u

typedef enum {
    LINK_OK = 0,
    LINK_ERR_ARG,
    LINK_ERR_FORMAT,
    LINK_ERR_UNDEFINED,
    LINK_ERR_DUPLICATE,
    LINK_ERR_PIC,
    LINK_ERR_ALIGN,
    LINK_ERR_DOUBLE_FIXUP,
    LINK_ERR_RANGE,
    LINK_ERR_FIXUP,
    LINK_ERR_LIMIT,
    LINK_ERR_EMPTY_TEXT,
    LINK_ERR_CHANGED,
    LINK_ERR_IO
} LinkStatus;

/* Diagnostic detail recorded by the link phases. kind selects which fields
 * are valid: 0 = plain message, 1 = symbol name, 2 = relocation detail. */
typedef struct {
    uint8_t kind;
    uint8_t reloc_type;
    uint8_t place_section;
    uint8_t target_section;
    uint32_t name_offset; /* offset into LinkLayout.pool */
    uint32_t name_len;    /* symbol name length (kind 1) */
    uint32_t offset;      /* section-relative relocation field offset */
} LinkError;

/* One input object: section bases and sizes in output space. The pass-1
 * sizes are the ledger pass 2 reconciles its re-opened headers against. */
typedef struct {
    uint32_t text_base;   /* offset in the concatenated output text */
    uint32_t data_base;   /* offset in private data ([data][bss]) */
    uint32_t bss_base;    /* running bss offset in pass 1; resolve adds
                             the final data total */
    uint32_t text_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t symbol_base; /* index of this object's first global symbol */
    uint32_t symbol_count;
    uint32_t reloc_count;
    uint32_t pool_size;
} LinkObject;

/* One input symbol, resolved in place to output-space values: section 1
 * values are output text offsets, 2/3 are private data offsets, 0 (abs)
 * are constants. Section 0xFF never survives lk_layout_resolve. */
typedef struct {
    uint32_t value;
    uint32_t name;   /* offset into LinkLayout.pool */
    uint16_t object; /* owning input number */
    uint8_t section;
    uint8_t flags;
} LinkSymbol;

/* Pass-1 state: object ledger, global symbol table, and name pool. All
 * input objects append symbols in input order. */
typedef struct {
    LinkObject objects[LINK_INPUT_MAX];
    uint32_t object_count;
    LinkSymbol symbols[LINK_SYMBOL_MAX];
    uint32_t symbol_count;
    char pool[LINK_NAME_POOL_MAX];
    uint32_t pool_size;
    uint32_t text_total;
    uint32_t data_total;
    uint32_t bss_total;
    LinkError error;
} LinkLayout;

/* S9M1 image under construction. Regions are contiguous in bytes in the
 * order the loader expects: header, code, init data, name, fixups, so the
 * whole image goes out with one ss9_file_store. */
typedef struct {
    uint8_t bytes[LINK_IMAGE_BUF_MAX];
    uint32_t text_size; /* concatenated text before 4-byte padding */
    uint32_t code_size; /* text_size padded to a 4-byte multiple */
    uint32_t data_size; /* concatenated init data before GOT padding */
    uint32_t init_size; /* max(4, data_size) */
    uint32_t bss_size;
    uint32_t name_size;
    uint32_t name_offset;
    uint32_t data_offset;
    uint32_t fixup_offset;
    uint32_t fixup_count;
    uint32_t image_size;
} LinkImage;

/* File I/O bound at startup so the link core builds for both the S9M1
 * module (ss9_* syscalls) and host unit drivers (stdio). */
typedef struct {
    void* (*open_read)(const char* path); /* handle or NULL */
    int32_t (*read)(void* handle, void* buffer, uint32_t length); /* <0 error */
    int32_t (*close)(void* handle); /* 0 on success */
} LinkIo;

typedef struct {
    void* handle;
    uint8_t buffer[LINK_READER_CHUNK];
    uint32_t fill;
    uint32_t position;
} LinkReader;

/* Parsed S9O1 header (validated by lk_obj_header). */
typedef struct {
    uint32_t text_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t symbol_count;
    uint32_t reloc_count;
    uint32_t pool_size;
} LinkObjHeader;

/* One validated S9O1 relocation record. */
typedef struct {
    uint32_t offset;
    uint32_t symbol;
    int32_t addend;
    uint8_t section; /* 1 = text, 2 = data */
    uint8_t type;    /* LINK_RELOC_* */
} LinkReloc;

/* --- readobj.c: sequential S9O1 input --- */

/* Opens a file for reading. LINK_ERR_IO when it cannot be opened. */
LinkStatus lk_reader_open(LinkReader* reader, const LinkIo* io,
                          const char* path);
/* Reads exactly length bytes. Short reads are LINK_ERR_FORMAT (truncated
 * object), read errors LINK_ERR_IO. */
LinkStatus lk_reader_read(LinkReader* reader, const LinkIo* io, void* out,
                          uint32_t length);
/* Discards length bytes. */
LinkStatus lk_reader_skip(LinkReader* reader, const LinkIo* io,
                          uint32_t length);
/* Confirms end of file: any trailing byte is LINK_ERR_FORMAT (the S9O1
 * file length must match the header fields). */
LinkStatus lk_reader_end(LinkReader* reader, const LinkIo* io);
void lk_reader_close(LinkReader* reader, const LinkIo* io);

/* Reads and validates the 40-byte S9O1 header: magic, version, flags=0,
 * entry=0, reserved=0, and a total size within the S9O1 limit. The file
 * length itself is checked by the exact reads plus lk_reader_end. */
LinkStatus lk_obj_header(LinkReader* reader, const LinkIo* io,
                         LinkObjHeader* out);
/* Reads and validates one relocation record: section in {1,2}, type
 * 1..4, symbol index in range, field offset within the section, and the
 * field width inside the section (4-byte fields need room; word fields
 * in text must sit on even offsets). */
LinkStatus lk_obj_reloc(LinkReader* reader, const LinkIo* io, LinkReloc* out,
                        const LinkObjHeader* header);

/* --- layout.c: pass 1 --- */

void lk_layout_init(LinkLayout* layout, uint32_t object_count);
/* Scans one input object: header checks, section base assignment with
 * the odd-text pad, symbol table and name pool build, relocation format
 * validation (relocations are read and discarded), duplicate global
 * definitions, and the per-stage capacity limits. */
LinkStatus lk_layout_input(LinkLayout* layout, const LinkIo* io,
                           const char* path, uint32_t object);
/* Resolves every symbol to output-space values (section + value):
 * defined locals via their object's bases, undefined/extern via global
 * definitions (same object first, then input order). Fails with
 * LINK_ERR_UNDEFINED and the symbol name recorded in layout->error. */
LinkStatus lk_layout_resolve(LinkLayout* layout);

/* --- fold.c: pass 2 --- */

/* Copies one input object into the image and applies its relocations:
 * reads header, text (into the code region), data (into the init data
 * region), then relocations, validating the header again. Generates
 * CODE32/DATA32 fixups for ABS32 in data, folds constants, and reports
 * PIC violations, misaligned or duplicate fixup slots, and range
 * errors per the contract's application table. */
LinkStatus lk_fold_input(LinkImage* image, LinkLayout* layout,
                         const LinkIo* io, const char* path,
                         uint32_t object);

/* --- image.c: output image --- */

void lk_image_init(LinkImage* image);
/* Computes the image geometry after pass 1 (code padding, data offsets,
 * name placement). Call once before folding. Returns LINK_ERR_EMPTY_TEXT
 * when the concatenated text is empty (the entry is text[0]) and
 * LINK_ERR_LIMIT when the code or data budget is exceeded. */
LinkStatus lk_image_plan(LinkImage* image, const LinkLayout* layout,
                         uint32_t name_size);
/* Zero-pads the code and init data regions, stores the module name,
 * fills the 80-byte S9M1 header (flags PIC|REENTRANT, entry 80, stack
 * 4096, dummy GOT 0/4), computes the IEEE CRC32 over the image with the
 * CRC field zeroed, and checks the final image size. */
LinkStatus lk_image_finish(LinkImage* image, const char* name,
                           uint32_t name_len);
/* Derives the module name from the output file name (extension stripped)
 * and validates it against the loader name rule. */
LinkStatus lk_module_name(const char* output, char* out);

/* Big-endian field helpers shared by the fold and image phases. */
void lk_put8(uint8_t* at, uint32_t value);
void lk_put16(uint8_t* at, uint32_t value);
void lk_put32(uint8_t* at, uint32_t value);

/* Renders the recorded error into out (NUL-terminated): fixed message,
 * "Undefined symbol NAME" / "Duplicate symbol NAME" (error kind 1), or
 * relocation detail such as "PIC violation: ABS32 in text at 16 targets
 * data" (error kind 2). Returns the status passed in. */
LinkStatus lk_error_message(LinkStatus status, const LinkError* error,
                            const LinkLayout* layout, char* out,
                            uint32_t capacity);

/* Fixed diagnostic strings for the plain status cases. */
const char* lk_status_message(LinkStatus status);

/* Length of the NUL-terminated pool name at offset, bounded by the pool
 * (no NUL before the pool end yields the remaining length). */
uint32_t lk_name_length(const LinkLayout* layout, uint32_t offset);

#endif
