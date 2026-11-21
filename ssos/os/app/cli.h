#ifndef SS_CLI_H
#define SS_CLI_H

/* Longest command line the shell accepts, including the terminating NUL.
 * Toolchain invocations such as "run test/ld OUT.O IN1.O IN2.O IN3.O" on a
 * subdirectory volume need more than 64 bytes. */
#define SS_CLI_LINE_MAX 96u

/* Executes a writable command line; returns 1 only for this shell's exit. */
int ss_cli_execute_line(char* line);

/* Feeds one key to the editable command line, echoing the redraw bytes.
 * line holds *used characters and keeps a terminating NUL; *cursor is the
 * insertion position. Returns 1 when the key was consumed (including
 * no-op cursor keys); unknown keys return 0. '\r', '\n', and 0x03 stay
 * with the caller. capacity is the buffer size including the NUL. */
int ss_cli_edit_key(int key, char* line, unsigned* used, unsigned* cursor,
                    unsigned capacity);

#endif
