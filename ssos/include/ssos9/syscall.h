#ifndef SSOS9_SYSCALL_H
#define SSOS9_SYSCALL_H

#include <stdint.h>

/* trap #0 ABI: D0 carries the number/result. D1 carries a request pointer or
 * the scalar argument for EXIT, KILL, and SLEEP. */
enum {
    SS9_SYS_VERSION = 0,
    SS9_SYS_GETPID = 1,
    SS9_SYS_GETPPID = 2,
    SS9_SYS_SPAWN = 3,
    SS9_SYS_EXIT = 4,
    SS9_SYS_WAIT = 5,
    SS9_SYS_KILL = 6,
    SS9_SYS_SLEEP = 7,
    SS9_SYS_PROC_INFO = 8,
    SS9_SYS_PROC_LIST = 9,
    SS9_SYS_OPEN = 0x20,
    SS9_SYS_CLOSE = 0x21,
    SS9_SYS_READ = 0x22,
    SS9_SYS_WRITE = 0x23
};

#define SS9_ABI_VERSION 1u
#define SS9_ERR (-1)
#define SS9_ERR_PARAM (-2)
#define SS9_ERR_NOSYS (-38)

typedef struct {
    uint32_t name; /* NUL-terminated module name */
    uint16_t argc;
    uint16_t reserved;
    uint32_t argv; /* array of argc 32-bit pointers */
} SS9SpawnRequest;

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint16_t exit_status;
    uint8_t state;
    uint8_t reserved;
    char name[32];
} SS9ProcessInfo;

typedef struct {
    uint32_t pid;
    uint32_t info;
} SS9ProcessInfoRequest;

typedef struct {
    uint16_t capacity;
    uint16_t count;
    uint32_t entries; /* array of SS9ProcessInfo */
} SS9ProcessListRequest;

typedef struct {
    uint32_t pid;
    uint16_t exit_status;
    uint16_t reserved;
} SS9WaitRequest;

#endif
