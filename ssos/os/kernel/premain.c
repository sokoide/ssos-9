#include "kernel.h"

#include <stdint.h>

extern uint8_t __bss_start, __bss_end;

static void clear_bss(void) {
    uint8_t* p = &__bss_start;
    while (p < &__bss_end) *p++ = 0;
}

void premain(void) {
    clear_bss();
    if (ss_init() != SS_OK) {
        (void)ss_irq_save();
        for (;;) {
        }
    }
    ss_set_interrupts();
    ss_run();
}
