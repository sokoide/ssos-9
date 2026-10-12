#include "cli_parse.h"

#include <stddef.h>
#include <string.h>

static int is_space(char c) { return c == ' ' || c == '\t'; }

int16_t ss_cli_parse_line(char* line, char* tokens[], uint16_t capacity,
                          int* background) {
    if (line == NULL || tokens == NULL || background == NULL)
        return SS_CLI_PARSE_SYNTAX;
    *background = 0;
    size_t length = strlen(line);
    while (length != 0 && is_space(line[length - 1])) length--;
    if (length != 0 && line[length - 1] == '&') {
        *background = 1;
        length--;
        while (length != 0 && is_space(line[length - 1])) length--;
    }
    line[length] = '\0';
    /* Reject internal/repeated '&' before dispatching any command. */
    for (size_t i = 0; i < length; i++)
        if (line[i] == '&')
            return SS_CLI_PARSE_SYNTAX;

    uint16_t count = 0;
    char* cursor = line;
    while (*cursor != '\0') {
        while (is_space(*cursor)) cursor++;
        if (*cursor == '\0')
            break;
        if (count == capacity || count == INT16_MAX)
            return SS_CLI_PARSE_LIMIT;
        tokens[count++] = cursor;
        while (*cursor != '\0' && !is_space(*cursor)) cursor++;
        if (*cursor != '\0')
            *cursor++ = '\0';
    }
    if (count == 0 && *background)
        return SS_CLI_PARSE_SYNTAX;
    return (int16_t)count;
}
