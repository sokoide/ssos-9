#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "editor_fake/ssos9/user.h"

int editor_main(int argc, char** argv);
static const char* keys;
static uint8_t file_data[16384], staged[16384];
static uint32_t file_size, read_offset, staged_size;
static int exists, fail_write, write_open, commits, fail_open_read, saw_failure, saw_unsaved;
static uint16_t screen_row, screen_column;
static uint8_t screen[25][80], last_screen[25][80];

void ss9_test_pause(void) { abort(); }
int32_t ss9_console_key(void) { return *keys ? (uint8_t)*keys++ : -1; }
int32_t ss9_console_clear(void) {
    memcpy(last_screen, screen, sizeof(screen));
    memset(screen, ' ', sizeof(screen));
    screen_row = screen_column = 0;
    return 0;
}
int32_t ss9_console_locate(uint16_t x, uint16_t y) {
    assert(x < 80 && y < 25); screen_row = y; screen_column = x; return 0;
}
int32_t ss9_open_read(const char* name) {
    assert(!strcmp(name, "TEST.S"));
    read_offset = 0;
    return exists && !fail_open_read ? 3 : -1;
}
int32_t ss9_open(const char* name, uint32_t flags) {
    assert(!strcmp(name, "TEST.S"));
    if ((flags == SS9_OPEN_CREATE && exists) ||
        (flags == SS9_OPEN_REPLACE && !exists)) return -1;
    staged_size = 0;
    write_open = 1;
    return 3;
}
int32_t ss9_read(uint32_t fd, void* buffer, uint32_t length) {
    assert(fd == 3 && !write_open);
    uint32_t n = file_size - read_offset;
    if (n > length) n = length;
    memcpy(buffer, file_data + read_offset, n);
    read_offset += n;
    return (int32_t)n;
}
int32_t ss9_write(uint32_t fd, const void* buffer, uint32_t length) {
    if (fd == 1) {
        const uint8_t* p = buffer;
        if (screen_row == 23)
            for (uint32_t i = 0; i + 18u <= length; i++)
                if (!memcmp(p + i, "Save failed: write", 18)) saw_failure = 1;
        if (screen_row == 23)
            for (uint32_t i = 0; i + 16u <= length; i++)
                if (!memcmp(p + i, "Unsaved: use :q!", 16)) saw_unsaved = 1;
        for (uint32_t i = 0; i < length && screen_column < 80; i++)
            screen[screen_row][screen_column++] = p[i];
        return (int32_t)length;
    }
    assert(fd == 3 && write_open && length <= sizeof(staged));
    if (fail_write) { write_open = 0; return -1; }
    memcpy(staged, buffer, length);
    staged_size = length;
    return (int32_t)length;
}
int32_t ss9_close(uint32_t fd) {
    assert(fd == 3);
    if (write_open) {
        memcpy(file_data, staged, staged_size);
        file_size = staged_size;
        exists = 1;
        commits++;
        write_open = 0;
    }
    return 0;
}

int main(void) {
    char* argv[] = {"edit", "TEST.S", 0};
    keys = "nihello\rworld\033:wq\r";
    assert(editor_main(2, argv) == 0);
    assert(exists && file_size == 11 && !memcmp(file_data, "hello\nworld", 11));
    assert(!memcmp(last_screen[0], "hello", 5));
    assert(!memcmp(last_screen[1], "world", 5));
    keys = "iX\033:wq\r";
    assert(editor_main(2, argv) == 0);
    assert(file_size == 12 && !memcmp(file_data, "Xhello\nworld", 12));
    assert(commits == 2);
    keys = "iY\033:q\r:wq\r";
    assert(editor_main(2, argv) == 0);
    assert(saw_unsaved && commits == 3 && file_size == 13 &&
           !memcmp(file_data, "YXhello\nworld", 13));
    fail_write = 1;
    keys = "iZ\033:wq\r:q!\r";
    assert(editor_main(2, argv) == 0);
    assert(saw_failure && commits == 3 && file_size == 13 && !memcmp(file_data, "YXhello\nworld", 13));
    fail_write = 0;
    fail_open_read = 1;
    keys = "niZ\033:wq\r:q!\r";
    assert(editor_main(2, argv) == 0);
    assert(commits == 3 && file_size == 13 && !memcmp(file_data, "YXhello\nworld", 13));
    return 0;
}
