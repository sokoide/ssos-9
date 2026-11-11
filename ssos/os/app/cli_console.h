#ifndef SS_CLI_CONSOLE_H
#define SS_CLI_CONSOLE_H

#include <stdint.h>

/* Shared across IOCS calls; the owner must not be reclaimed while locked. */
void ss_iocs_lock(void);
void ss_iocs_unlock(void);

void ss_cli_console_print(const char* text);
void ss_cli_console_write(const uint8_t* bytes, uint32_t length);
void ss_cli_console_clear(void);
int ss_cli_console_locate(uint32_t x, uint32_t y);
int ss_cli_console_read_key(void);
void ss_cli_console_mux_enable(void);
int ss_cli_console_mux_select(uint8_t window);
uint8_t ss_cli_console_mux_active(void);

#endif
