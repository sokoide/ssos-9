/* Freestanding support routines that GCC may emit calls to at -Os.
 * memcpy: block moves (struct assignment). The 32-bit multiply/divide
 * helpers replace the m68k libgcc routines, whose objects carry absolute
 * relocations the S9M1 module profile cannot carry (code-only, PC16).
 * Same implementation as apps/asm/support.c; modules build independently. */

#include <stddef.h>
#include <stdint.h>

void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n-- > 0) *d++ = *s++;
    return dst;
}

/* Restoring division over the 32 bits of num. den == 0 returns 0. */
static uint32_t udivmod(uint32_t num, uint32_t den, uint32_t* rem) {
    uint32_t q = 0;
    uint32_t r = 0;
    int i;
    if (rem != NULL)
        *rem = 0;
    if (den == 0)
        return 0;
    for (i = 31; i >= 0; i--) {
        r = (r << 1) | ((num >> i) & 1u);
        if (r >= den) {
            r -= den;
            q |= 1u << i;
        }
    }
    if (rem != NULL)
        *rem = r;
    return q;
}

uint32_t __udivsi3(uint32_t a, uint32_t b) { return udivmod(a, b, NULL); }

uint32_t __umodsi3(uint32_t a, uint32_t b) {
    uint32_t r;
    udivmod(a, b, &r);
    return r;
}

int32_t __divsi3(int32_t a, int32_t b) {
    uint32_t ua = (a < 0) ? -(uint32_t)a : (uint32_t)a;
    uint32_t ub = (b < 0) ? -(uint32_t)b : (uint32_t)b;
    int neg = (a < 0) != (b < 0);
    uint32_t q = udivmod(ua, ub, NULL);
    return neg ? -(int32_t)q : (int32_t)q;
}

int32_t __modsi3(int32_t a, int32_t b) {
    uint32_t ua = (a < 0) ? -(uint32_t)a : (uint32_t)a;
    uint32_t ub = (b < 0) ? -(uint32_t)b : (uint32_t)b;
    uint32_t r;
    udivmod(ua, ub, &r);
    return (a < 0) ? -(int32_t)r : (int32_t)r;
}

int32_t __mulsi3(int32_t a, int32_t b) {
    uint32_t ua = (a < 0) ? -(uint32_t)a : (uint32_t)a;
    uint32_t ub = (b < 0) ? -(uint32_t)b : (uint32_t)b;
    uint32_t r = 0;
    while (ub != 0) {
        if (ub & 1u)
            r += ua;
        ua <<= 1;
        ub >>= 1;
    }
    return ((a < 0) != (b < 0)) ? -(int32_t)r : (int32_t)r;
}
