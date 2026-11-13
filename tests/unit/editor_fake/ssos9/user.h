#ifndef EDIT_TEST_USER_H
#define EDIT_TEST_USER_H
#include <stdint.h>
#define SS9_OPEN_CREATE 1u
#define SS9_OPEN_REPLACE 2u
#define SS9_CONSOLE_COLUMNS 80u
#define SS9_CONSOLE_ROWS 31u
int32_t ss9_console_key(void);
int32_t ss9_console_clear(void);
int32_t ss9_console_size(void);
int32_t ss9_console_locate(uint16_t x, uint16_t y);
int32_t ss9_open_read(const char* name);
int32_t ss9_open(const char* name, uint32_t flags);
int32_t ss9_read(uint32_t fd, void* buffer, uint32_t length);
int32_t ss9_write(uint32_t fd, const void* buffer, uint32_t length);
int32_t ss9_close(uint32_t fd);
#endif
