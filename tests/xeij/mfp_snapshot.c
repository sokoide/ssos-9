#include <stdint.h>
#include <stdio.h>
#include <x68k/iocs.h>

static unsigned read_byte(uintptr_t address) {
    return *(volatile const uint8_t *)address;
}

static unsigned long read_vector(uintptr_t address) {
    uint32_t value;
    /* The exception table is outside C's object address space. */
    __asm__ volatile ("move.l (%1),%0" : "=d"(value) : "a"(address) : "memory");
    return value;
}

int main(void) {
    int old_ssp = _iocs_b_super(0);
    unsigned ier_b = read_byte(0xe88009);
    unsigned imr_b = read_byte(0xe88015);
    unsigned tcdcr = read_byte(0xe8801d);
    /* A running Timer D exposes a changing counter through TDDR. */
    unsigned tddr = (tcdcr & 7) ? 0 : read_byte(0xe88025);
    unsigned long timer_d = read_vector(0x110);
    unsigned long trap0 = read_vector(0x080);
    unsigned long trap13 = read_vector(0x0b4);

    (void)_iocs_b_super(old_ssp);
    printf("MFPSTATE=%02X/%02X/%02X/%02X/%08lX/%08lX/%08lX\r\n",
           ier_b, imr_b, tcdcr, tddr, timer_d, trap0, trap13);
    return 0;
}
