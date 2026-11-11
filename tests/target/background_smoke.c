/* Emit numbered records while other mux windows are active. */
#include "ssos9/user.h"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    char line[] = {'B', 'G', ' ', '0', '0', '\r', '\n'};
    for (unsigned i = 0; i < 60; i++) {
        if (ss9_write(1, line, sizeof(line)) != sizeof(line)) return 1;
        register uint32_t d0 __asm__("d0") = SS9_SYS_SLEEP;
        register uint32_t d1 __asm__("d1") = 100;
        __asm__ volatile("trap #0" : "+d"(d0), "+d"(d1) : : "cc", "memory");
        if ((int32_t)d0 != 0) return 2;
        if (++line[4] > '9') {
            line[4] = '0';
            line[3]++;
        }
    }
    return 0;
}
