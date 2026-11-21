/* Minimal freestanding string.h for the QEMU scheduler test.
 * m68k-elf-gcc here has no newlib, so we declare only what scheduler.c uses.
 * memset/memcpy/memmove are implemented in stub.c. */
#ifndef SS_TEST_STRING_H
#define SS_TEST_STRING_H

#include <stddef.h>

void* memset(void* s, int c, size_t n);
void* memcpy(void* dst, const void* src, size_t n);
void* memmove(void* dst, const void* src, size_t n);
size_t strlen(const char* text);
int strcmp(const char* a, const char* b);
char* strcat(char* dst, const char* src);

#endif
