/* Manual S9M1 console test. Run in the foreground inside mux. */
#include "ssos9/user.h"

static void pause_tick(void) {
    register uint32_t d0 __asm__("d0") = SS9_SYS_SLEEP;
    register uint32_t d1 __asm__("d1") = 1;
    __asm__ volatile("trap #0" : "+d"(d0), "+d"(d1) : : "cc", "memory");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    char label[] = {'K', 'E', 'Y', ':', ' ', '0', '0', '0', '0'};
    if (ss9_console_clear()) return 1;
    for (;;) {
        int32_t key = ss9_console_key();
        if (key < 0) {
            pause_tick();
            continue;
        }
        if (key == 'q') return 0;
        for (unsigned i = 0; i < 4; i++) {
            unsigned nibble = ((uint32_t)key >> (12 - 4 * i)) & 15u;
            label[5 + i] = (char)(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
        }
        if (ss9_console_locate(2, 6) || ss9_write(1, label, sizeof(label)) < 0)
            return 2;
    }
}
