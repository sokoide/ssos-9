#ifndef SSOS9_USER_H
#define SSOS9_USER_H

#include "syscall.h"

static inline int32_t ss9_open(const char* name, uint32_t flags) {
    SS9OpenRequest request __attribute__((aligned(4))) = {
        (uint32_t)(uintptr_t)name, flags
    };
    register uint32_t d0 __asm__("d0") = SS9_SYS_OPEN;
    register uint32_t d1 __asm__("d1") = (uint32_t)(uintptr_t)&request;
    __asm__ volatile("trap #0" : "+d"(d0), "+d"(d1) : : "cc", "memory");
    return (int32_t)d0;
}

static inline int32_t ss9_open_read(const char* name) {
    return ss9_open(name, SS9_OPEN_READ);
}

static inline int32_t ss9_read(uint32_t fd, void* buffer, uint32_t length) {
    SS9ReadRequest request __attribute__((aligned(4))) = {
        fd, (uint32_t)(uintptr_t)buffer, length
    };
    register uint32_t d0 __asm__("d0") = SS9_SYS_READ;
    register uint32_t d1 __asm__("d1") = (uint32_t)(uintptr_t)&request;
    __asm__ volatile("trap #0" : "+d"(d0), "+d"(d1) : : "cc", "memory");
    return (int32_t)d0;
}

static inline int32_t ss9_close(uint32_t fd) {
    register uint32_t d0 __asm__("d0") = SS9_SYS_CLOSE;
    register uint32_t d1 __asm__("d1") = fd;
    __asm__ volatile("trap #0" : "+d"(d0), "+d"(d1) : : "cc", "memory");
    return (int32_t)d0;
}

/* The MC68000 C ABI only guarantees two-byte stack alignment; request
 * structures crossing the SSOS syscall boundary require four. */
static inline int32_t ss9_write(uint32_t fd, const void* buffer,
                                uint32_t length) {
    SS9WriteRequest request __attribute__((aligned(4)));
    request.fd = fd;
    request.buffer = (uint32_t)(uintptr_t)buffer;
    request.length = length;
    register uint32_t d0 __asm__("d0") = SS9_SYS_WRITE;
    register uint32_t d1 __asm__("d1") = (uint32_t)(uintptr_t)&request;
    __asm__ volatile("trap #0" : "+d"(d0), "+d"(d1) : : "cc", "memory");
    return (int32_t)d0;
}

static inline int32_t ss9_file_transfer(uint32_t operation, const char* name,
                                        void* buffer, uint32_t size,
                                        uint32_t flags) {
    SS9FileRequest request __attribute__((aligned(4)));
    request.name = (uint32_t)(uintptr_t)name;
    request.buffer = (uint32_t)(uintptr_t)buffer;
    request.size = size;
    request.flags = flags;
    register uint32_t d0 __asm__("d0") = operation;
    register uint32_t d1 __asm__("d1") = (uint32_t)(uintptr_t)&request;
    __asm__ volatile("trap #0" : "+d"(d0), "+d"(d1) : : "cc", "memory");
    return (int32_t)d0;
}

static inline int32_t ss9_file_load(const char* name, void* buffer,
                                    uint32_t capacity) {
    return ss9_file_transfer(SS9_SYS_FILE_LOAD, name, buffer, capacity, 0);
}

static inline int32_t ss9_file_store(const char* name, const void* buffer,
                                     uint32_t size, uint32_t flags) {
    return ss9_file_transfer(SS9_SYS_FILE_STORE, name, (void*)buffer, size, flags);
}

#endif
