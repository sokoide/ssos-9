#include "../../apps/edit/buffer.h"
#include <assert.h>
#include <string.h>

int main(void) {
    EditBuffer e;
    edit_init(&e);
    const char* text = "abc\nx\nhello";
    for (unsigned i = 0; text[i]; i++) assert(edit_insert(&e, (uint8_t)text[i]) == 0);
    assert(e.modified && e.cursor == 11 && e.length == 11);
    edit_up(&e); assert(e.cursor == 5);
    edit_up(&e); assert(e.cursor == 1);
    edit_down(&e); assert(e.cursor == 5);
    edit_down(&e); assert(e.cursor == 7);
    edit_left(&e); assert(e.cursor == 6);
    edit_left(&e); assert(e.cursor == 6);
    edit_right(&e); assert(e.cursor == 7);
    assert(edit_backspace(&e) == 0 && e.cursor == 6);
    assert(memcmp(e.data, "abc\nx\nello", 10) == 0);
    assert(edit_delete(&e) == 0 && e.length == 9);
    edit_init(&e);
    for (uint32_t i = 0; i < EDIT_CAPACITY; i++) assert(edit_insert(&e, 'z') == 0);
    assert(edit_insert(&e, 'q') == -1 && e.length == EDIT_CAPACITY);
    assert(e.data[0] == 'z' && e.data[EDIT_CAPACITY - 1] == 'z');
    return 0;
}
