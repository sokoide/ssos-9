/* QEMU hardware stubs and minimal freestanding runtime. */
#include "scheduler.h"

#include <stddef.h>
#include <stdint.h>

volatile uint32_t ss_tick_counter;
uint8_t __ssosstack_start;

static uint8_t stack_arena[SS_MAX_TASKS * SS_TASK_STACK]
    __attribute__((aligned(4)));
uint8_t* ss_task_stack_base = stack_arena;

void* memset(void* dst, int value, size_t size) {
    uint8_t* bytes = dst;
    while (size-- != 0) *bytes++ = (uint8_t)value;
    return dst;
}

void* memcpy(void* dst, const void* src, size_t size) {
    uint8_t* out = dst;
    const uint8_t* in = src;
    while (size-- != 0) *out++ = *in++;
    return dst;
}

size_t strlen(const char* text) {
    size_t n = 0;
    while (text[n]) n++;
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

char* strcat(char* dest, const char* source) {
    char* out = dest + strlen(dest);
    while ((*out++ = *source++) != '\0') {}
    return dest;
}

#define GOLDFISH_TTY ((volatile uint32_t*)0xff008000)

void tty_putc(char c) { *GOLDFISH_TTY = (uint32_t)(uint8_t)c; }

void tty_puts(const char* s) {
    while (*s != '\0') tty_putc(*s++);
}

void tty_putu(unsigned value) {
    char buf[8];
    int i = (int)sizeof(buf);
    buf[--i] = '\0';
    if (value == 0) {
        buf[--i] = '0';
    } else {
        while (value != 0) {
            buf[--i] = (char)('0' + value % 10);
            value /= 10;
        }
    }
    tty_puts(&buf[i]);
}

/* Captured IOCS bytes let the process integration test inspect console writes. */
uint8_t ss_test_console_bytes[256];
uint32_t ss_test_console_count;
int _iocs_b_putc(int ch);
void _iocs_b_print(const char* text) {
    while (*text) _iocs_b_putc((uint8_t)*text++);
}
int _iocs_b_putc(int ch) {
    if (ss_test_console_count < sizeof(ss_test_console_bytes))
        ss_test_console_bytes[ss_test_console_count++] = (uint8_t)ch;
    return ch;
}
__attribute__((weak)) int _iocs_b_read(int mode, int position, int size,
                                       void* out) {
    (void)mode; (void)position; (void)size; (void)out;
    return -1;
}

__attribute__((weak)) void ss_cli_console_write(const uint8_t* bytes,
                                                uint32_t length) {
    (void)bytes;
    (void)length;
}
__attribute__((weak)) int32_t ss_system_file_load(const char* name, uint8_t* data,
                                                   uint32_t capacity) {
    (void)name; (void)data; (void)capacity;
    return -1;
}
__attribute__((weak)) int32_t ss_system_file_store(const char* name,
                                                    const uint8_t* data,
                                                    uint32_t size,
                                                    uint32_t replace) {
    (void)name; (void)data; (void)size; (void)replace;
    return -1;
}
__attribute__((weak)) int32_t ss_system_open_reader(uint32_t owner, const char* name) {
    (void)owner; (void)name; return -1;
}
__attribute__((weak)) int32_t ss_system_open_writer(uint32_t owner, const char* name,
                                                    int replace) {
    (void)owner; (void)name; (void)replace; return -1;
}
__attribute__((weak)) int32_t ss_system_write_fd(uint32_t owner, uint32_t fd,
                                                 const uint8_t* data, uint32_t size) {
    (void)owner; (void)fd; (void)data; (void)size; return -1;
}
__attribute__((weak)) int32_t ss_system_read_fd(uint32_t owner, uint32_t fd,
                                                uint8_t* data, uint32_t size) {
    (void)owner; (void)fd; (void)data; (void)size; return -1;
}
__attribute__((weak)) int32_t ss_system_close_fd(uint32_t owner, uint32_t fd) {
    (void)owner; (void)fd; return -1;
}
__attribute__((weak)) void ss_system_close_process_files(uint32_t owner) { (void)owner; }
void _iocs_b_clr_st(void) {}
void _iocs_b_locate(int x, int y) {
    (void)x;
    (void)y;
}
