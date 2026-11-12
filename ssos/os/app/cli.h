#ifndef SS_CLI_H
#define SS_CLI_H

/* Longest command line the shell accepts, including the terminating NUL.
 * Toolchain invocations such as "run test/ld OUT.O IN1.O IN2.O IN3.O" on a
 * subdirectory volume need more than 64 bytes. */
#define SS_CLI_LINE_MAX 96u

/* Executes a writable command line; returns 1 only for this shell's exit. */
int ss_cli_execute_line(char* line);

#endif
