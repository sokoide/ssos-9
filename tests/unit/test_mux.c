#include "../../ssos/os/app/mux.h"

#include <assert.h>
#include <string.h>

int main(void) {
    SSMux mux;
    ss_mux_init(&mux);
    assert(ss_mux_write(&mux, 0, (const uint8_t*)"one\r\ntwo", 8) == 0);
    assert(ss_mux_write(&mux, 1, (const uint8_t*)"other", 5) == 0);
    assert(memcmp(mux.windows[0].cells[0], "one", 3) == 0);
    assert(memcmp(mux.windows[0].cells[1], "two", 3) == 0);
    assert(memcmp(mux.windows[1].cells[0], "other", 5) == 0);
    assert(ss_mux_key_push(&mux, 'a') == 0);
    assert(ss_mux_select(&mux, 1) == 0);
    assert(ss_mux_key_push(&mux, 'b') == 0);
    assert(ss_mux_key_pop(&mux, 1) == 'b');
    assert(ss_mux_key_pop(&mux, 0) == 'a');
    assert(ss_mux_key_pop(&mux, 0) == -1);
    for (unsigned i = 0; i < SS_MUX_KEY_CAPACITY; i++)
        assert(ss_mux_key_push(&mux, (uint8_t)i) == 0);
    assert(ss_mux_key_push(&mux, 0) == -1);
    for (unsigned i = 0; i < SS_MUX_KEY_CAPACITY; i++)
        assert(ss_mux_key_pop(&mux, 1) == (int)i);
    assert(ss_mux_select(&mux, SS_MUX_WINDOWS) == -1);
    ss_mux_init(&mux);
    for (unsigned y = 0; y < SS_MUX_ROWS; y++)
        assert(ss_mux_write(&mux, 2, (const uint8_t*)"x\r\n", 3) == 0);
    assert(mux.windows[2].y == SS_MUX_ROWS - 1);
    assert(mux.windows[2].cells[0][0] == 'x');
    assert(mux.windows[2].cells[SS_MUX_ROWS - 1][0] == ' ');
    return 0;
}
