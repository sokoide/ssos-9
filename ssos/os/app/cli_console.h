#ifndef SS_CLI_CONSOLE_H
#define SS_CLI_CONSOLE_H

#include <stdint.h>

/* Shared across IOCS calls; the owner must not be reclaimed while locked. */
void ss_iocs_lock(void);
void ss_iocs_unlock(void);

void ss_cli_console_print(const char* text);
void ss_cli_console_write(const uint8_t* bytes, uint32_t length);
void ss_cli_console_clear(void);
uint32_t ss_cli_console_size(void);
int ss_cli_console_locate(uint32_t x, uint32_t y);
int ss_cli_console_read_key(void);
void ss_cli_console_mux_enable(void);
int ss_cli_console_mux_select(uint8_t window);
uint8_t ss_cli_console_mux_active(void);

#if defined(SS_HOST_TEST) || defined(SS_QEMU_TEST)
/* B_CONSOL result stand-ins for host harnesses, mirroring the IOCS layout
 * (d1 start offset, d2 packed columns-1 / rows-1). Both zero keeps the
 * synthetic default region, matching pre-query behavior. */
extern uint32_t ss_test_console_offset;
extern uint32_t ss_test_console_d2;
#endif

#endif
