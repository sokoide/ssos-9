#ifndef SS_EDIT_BUFFER_H
#define SS_EDIT_BUFFER_H

#include <stdint.h>

#define EDIT_CAPACITY 16384u

typedef struct {
    uint8_t data[EDIT_CAPACITY];
    uint32_t length;
    uint32_t cursor;
    uint8_t modified;
} EditBuffer;

void edit_init(EditBuffer* edit);
int edit_insert(EditBuffer* edit, uint8_t c);
int edit_delete(EditBuffer* edit);
int edit_backspace(EditBuffer* edit);
void edit_left(EditBuffer* edit);
void edit_right(EditBuffer* edit);
void edit_up(EditBuffer* edit);
void edit_down(EditBuffer* edit);
uint32_t edit_line_start(const EditBuffer* edit, uint32_t position);
uint32_t edit_line_end(const EditBuffer* edit, uint32_t position);

#endif
