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
    unsigned ier_a = read_byte(0xe88007);
    unsigned imr_a = read_byte(0xe88013);
    unsigned tacr = read_byte(0xe88019);
    unsigned tbcr = read_byte(0xe8801f);
    unsigned vr = read_byte(0xe88017);
    unsigned long timer_d = read_vector(0x110);
    unsigned long trap0 = read_vector(0x080);
    unsigned long trap13 = read_vector(0x0b4);
    unsigned long timer_a = read_vector(0x134);
    unsigned long key = read_vector(0x130);
    unsigned long crtc_lo = read_vector(0x138);
    unsigned long crtc_hi = read_vector(0x13c);

    (void)_iocs_b_super(old_ssp);
    /* The line is longer than the 80-column text screen and wraps on the
     * guest; the itester strips whitespace and row breaks before matching.
     * Byte fields and dword fields are told apart by width in the MFPSTATE
     * regex. The register set matches every register mfp_state.s saves and
     * restores; TADR appears in neither (a running counter is not a stable
     * comparison value). */
    printf("MFPSTATE=%02X/%02X/%02X/%02X/%02X/%02X/%02X/%02X/%02X/"
           "%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX\r\n",
           ier_b, imr_b, tcdcr, tddr, ier_a, imr_a, tacr, tbcr, vr,
           timer_d, trap0, trap13, timer_a, key, crtc_lo, crtc_hi);
    return 0;
}
