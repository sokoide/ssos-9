/* QEMU hardware stubs and minimal freestanding runtime. */
#include "scheduler.h"

#include <stddef.h>
#include <stdint.h>

volatile uint32_t ss_tick_counter;

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
