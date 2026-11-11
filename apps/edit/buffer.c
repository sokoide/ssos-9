#include "buffer.h"

void edit_init(EditBuffer* edit) {
    edit->length = 0;
    edit->cursor = 0;
    edit->modified = 0;
}

uint32_t edit_line_start(const EditBuffer* edit, uint32_t pos) {
    while (pos && edit->data[pos - 1] != '\n') pos--;
    return pos;
}

uint32_t edit_line_end(const EditBuffer* edit, uint32_t pos) {
    while (pos < edit->length && edit->data[pos] != '\n') pos++;
    return pos;
}

int edit_insert(EditBuffer* edit, uint8_t c) {
    if (edit->length == EDIT_CAPACITY) return -1;
    for (uint32_t i = edit->length; i > edit->cursor; i--)
        edit->data[i] = edit->data[i - 1];
    edit->data[edit->cursor++] = c;
    edit->length++;
    edit->modified = 1;
    return 0;
}

int edit_delete(EditBuffer* edit) {
    if (edit->cursor >= edit->length) return -1;
    for (uint32_t i = edit->cursor + 1; i < edit->length; i++)
        edit->data[i - 1] = edit->data[i];
    edit->length--;
    edit->modified = 1;
    return 0;
}

int edit_backspace(EditBuffer* edit) {
    if (!edit->cursor) return -1;
    edit->cursor--;
    return edit_delete(edit);
}

void edit_left(EditBuffer* edit) {
    if (edit->cursor > edit_line_start(edit, edit->cursor)) edit->cursor--;
}

void edit_right(EditBuffer* edit) {
    if (edit->cursor < edit_line_end(edit, edit->cursor)) edit->cursor++;
}

void edit_up(EditBuffer* edit) {
    uint32_t start = edit_line_start(edit, edit->cursor);
    if (!start) return;
    uint32_t column = edit->cursor - start;
    uint32_t previous = edit_line_start(edit, start - 1);
    uint32_t length = start - 1 - previous;
    edit->cursor = previous + (column < length ? column : length);
}

void edit_down(EditBuffer* edit) {
    uint32_t start = edit_line_start(edit, edit->cursor);
    uint32_t end = edit_line_end(edit, edit->cursor);
    if (end == edit->length) return;
    uint32_t column = edit->cursor - start;
    uint32_t next = end + 1;
    uint32_t length = edit_line_end(edit, next) - next;
    edit->cursor = next + (column < length ? column : length);
}
