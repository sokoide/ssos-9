#ifndef SS_SYSCALL_H
#define SS_SYSCALL_H

#include <stdint.h>

int32_t ss_syscall_dispatch(uint32_t number, uint32_t request);

#endif
