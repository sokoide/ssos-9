#ifndef SS_CLI_PARSE_H
#define SS_CLI_PARSE_H

#include <stdint.h>

#define SS_CLI_PARSE_SYNTAX (-1)
#define SS_CLI_PARSE_LIMIT (-2)

/* Splits a writable line in place. A single trailing '&' is independent of
 * whitespace. Quotes, escapes, and command lists are not supported yet.
 * Returns the token count, or a negative parse error; dispatch only on success.
 */
int16_t ss_cli_parse_line(char* line, char* tokens[], uint16_t capacity,
                          int* background);

#endif
