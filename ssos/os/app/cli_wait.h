#ifndef SS_CLI_WAIT_H
#define SS_CLI_WAIT_H

#include "../process/process.h"

#define SS_CLI_WAIT_CANCELLED 1

extern volatile uint8_t ss_ctrl_c_pending;

void ss_cli_set_foreground_pid(SSProcessId pid);
int16_t ss_cli_wait_pid(SSProcessId pid, uint16_t* exit_status,
                        int* interrupted);
int ss_cli_poll_ctrl_c(void);
int ss_cli_next_key(void);

#endif
