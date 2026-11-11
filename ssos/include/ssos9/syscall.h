#ifndef SSOS9_SYSCALL_H
#define SSOS9_SYSCALL_H

#include <stdint.h>

/* trap #0 ABI: D0 carries the number/result. D1 carries a request pointer or
 * the scalar argument for EXIT, KILL, SLEEP, CLOSE, and CONSOLE_LOCATE.
 * Module requests/inputs must reside in private memory or the retained image;
 * outputs must reside in private memory. Structured buffers are 4-byte aligned.
 * Strings must terminate within their allowed region and the ABI size limits.
 * PID 1 is trusted and may pass main RAM buffers in [0x1000, 0xc00000).
 * These checks do not provide memory isolation for Supervisor-mode code. */
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
    SS9_SYS_WRITE = 0x23,
    SS9_SYS_FILE_LOAD = 0x24,
    SS9_SYS_FILE_STORE = 0x25,
    SS9_SYS_CONSOLE_CLEAR = 0x26,
    SS9_SYS_CONSOLE_LOCATE = 0x27,
    SS9_SYS_CONSOLE_KEY = 0x28
};

#define SS9_ABI_VERSION 1u
#define SS9_ERR (-1)
#define SS9_ERR_PARAM (-2)
#define SS9_ERR_NOSYS (-38)
#define SS9_WRITE_MAX 256u
#define SS9_FILE_MAX 65536u
#define SS9_FILE_CREATE 0u
#define SS9_FILE_REPLACE 1u
#define SS9_OPEN_READ 0u
#define SS9_OPEN_CREATE 1u
#define SS9_OPEN_REPLACE 2u
#define SS9_READ_MAX 65536u
#define SS9_FILE_WRITE_MAX 65536u
#define SS9_CONSOLE_COLUMNS 80u
#define SS9_CONSOLE_ROWS 25u

/* CONSOLE_CLEAR and CONSOLE_KEY take scalar zero. CONSOLE_LOCATE packs x in
 * the high 16 bits and y in the low 16 bits. CONSOLE_KEY is nonblocking:
 * ASCII returns as 0x00xx, non-ASCII IOCS scan codes as scan << 8, and -1 means
 * no key or the caller is not the foreground process. */

typedef struct {
    uint32_t name;
    uint32_t flags;
} SS9OpenRequest;

typedef struct {
    uint32_t fd;
    uint32_t buffer;
    uint32_t length;
} SS9ReadRequest;

/* Whole-file transfer; names are root-directory ASCII 8.3 strings. */
typedef struct {
    uint32_t name;
    uint32_t buffer;
    uint32_t size;
    uint32_t flags;
} SS9FileRequest;

typedef struct {
    uint32_t fd;
    uint32_t buffer;
    uint32_t length;
} SS9WriteRequest;

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
