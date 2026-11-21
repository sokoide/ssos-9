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
    assert(ss_mux_locate(&mux, 1, 10, 5) == 0);
    assert(ss_mux_write(&mux, 1, (const uint8_t*)"Z", 1) == 0);
    assert(mux.windows[1].cells[5][10] == 'Z');
    assert(mux.windows[0].x == 3 && mux.windows[0].y == 1);
    assert(ss_mux_locate(&mux, 1, SS_MUX_COLUMNS, 0) == -1);
    assert(ss_mux_locate(&mux, 1, 0, SS_MUX_ROWS) == -1);
    assert(mux.windows[1].x == 11 && mux.windows[1].y == 5);
    assert(ss_mux_key_push(&mux, 'a') == 0);
    assert(ss_mux_select(&mux, 1) == 0);
    assert(ss_mux_key_push(&mux, 'b') == 0);
    assert(ss_mux_key_pop(&mux, 1) == 'b');
    assert(ss_mux_key_pop(&mux, 0) == 'a');
    assert(ss_mux_key_pop(&mux, 0) == -1);
    assert(ss_mux_key_event(&mux, 0x6400) == 0 && mux.active == 1);
    assert(ss_mux_key_event(&mux, 0x6500) == 1 && mux.active == 2);
    assert(ss_mux_key_event(&mux, 0x6300) == 1 && mux.active == 0);
    const uint16_t input[] = {0x1d, '2', '~', '3', 0x3b00};
    for (unsigned i = 0; i < sizeof(input) / sizeof(input[0]); i++)
        assert(ss_mux_key_event(&mux, input[i]) == 0 && mux.active == 0);
    for (unsigned i = 0; i < sizeof(input) / sizeof(input[0]); i++)
        assert(ss_mux_key_pop(&mux, 0) == input[i]);
    for (unsigned i = 0; i < SS_MUX_KEY_CAPACITY; i++)
        assert(ss_mux_key_push(&mux, (uint8_t)i) == 0);
    assert(ss_mux_key_event(&mux, 'x') == -1);
    assert(ss_mux_key_event(&mux, 0x6500) == 1 && mux.active == 2);
    assert(ss_mux_key_event(&mux, 0x6300) == 1 && mux.active == 0);
    for (unsigned i = 0; i < SS_MUX_KEY_CAPACITY; i++)
        assert(ss_mux_key_pop(&mux, 0) == (int)i);
    assert(ss_mux_select(&mux, SS_MUX_WINDOWS) == -1);
    ss_mux_init(&mux);
    for (unsigned y = 0; y < SS_MUX_ROWS; y++)
        assert(ss_mux_write(&mux, 2, (const uint8_t*)"x\r\n", 3) == 0);
    assert(mux.windows[2].y == SS_MUX_ROWS - 1);
    assert(mux.windows[2].scroll_count == 1);
    assert(mux.windows[2].cells[0][0] == 'x');
    assert(mux.windows[2].cells[SS_MUX_ROWS - 1][0] == ' ');
    /* The console region decides the window height: a 31 kHz screen keeps
     * rows 25..30 usable and scrolls one row later. */
    assert(ss_mux_set_rows(&mux, 0) == -1);
    assert(ss_mux_set_rows(&mux, SS_MUX_ROWS_MAX + 1u) == -1);
    assert(ss_mux_set_rows(&mux, SS_MUX_ROWS_MAX) == 0);
    assert(ss_mux_locate(&mux, 2, 0, SS_MUX_ROWS_MAX - 1u) == 0);
    assert(ss_mux_locate(&mux, 2, 0, SS_MUX_ROWS_MAX) == -1);
    assert(ss_mux_locate(&mux, 2, 0, 0) == 0);
    for (unsigned y = 0; y < SS_MUX_ROWS_MAX; y++)
        assert(ss_mux_write(&mux, 2, (const uint8_t*)"y\r\n", 3) == 0);
    assert(mux.windows[2].y == SS_MUX_ROWS_MAX - 1u);
    assert(mux.windows[2].scroll_count == 1);
    assert(mux.windows[2].cells[0][0] == 'y');
    assert(mux.windows[2].cells[SS_MUX_ROWS_MAX - 1u][0] == ' ');
    /* UI-01: B_CONSOL region decoding. d2.hw = columns-1, d2.w = rows-1,
     * d1.l = display start offset (X/8+Y*128). The standard 80x25 region. */
    SSMuxRegion region;
    assert(ss_mux_region_decode(0, (79u << 16) | 24u, &region) == 0);
    assert(region.rows == SS_MUX_ROWS && region.columns == SS_MUX_COLUMNS &&
           region.offset_supported == 1);
    /* A narrower region narrows the mux width; 31 rows stay exact. */
    assert(ss_mux_region_decode(0, (39u << 16) | 30u, &region) == 0);
    assert(region.rows == SS_MUX_ROWS_MAX && region.columns == 40);
    /* Rows beyond the 31-row text screen and columns beyond the buffer
     * clamp to the defaults (documented fallback, not silent truncation). */
    assert(ss_mux_region_decode(0, (79u << 16) | 63u, &region) == 0);
    assert(region.rows == SS_MUX_ROWS && region.columns == SS_MUX_COLUMNS);
    assert(ss_mux_region_decode(0, (127u << 16) | 24u, &region) == 0);
    assert(region.columns == SS_MUX_COLUMNS);
    /* A nonzero start offset is unsupported (absolute 0,0 drawing model):
     * the caller warns and keeps the fixed width; rows stay variable. */
    assert(ss_mux_region_decode(128, (39u << 16) | 24u, &region) == 0);
    assert(region.offset_supported == 0 && region.rows == SS_MUX_ROWS &&
           region.columns == SS_MUX_COLUMNS);
    assert(ss_mux_set_columns(&mux, 0) == -1);
    assert(ss_mux_set_columns(&mux, SS_MUX_COLUMNS + 1u) == -1);
    assert(ss_mux_set_columns(&mux, 40) == 0 && mux.columns == 40);
    assert(ss_mux_locate(&mux, 1, 39, 0) == 0);
    assert(ss_mux_locate(&mux, 1, 40, 0) == -1);
    uint8_t line[41];
    memset(line, 'W', 40);
    line[40] = 'Z';
    assert(ss_mux_clear(&mux, 1) == 0); /* set_columns cleared window 1 */
    assert(ss_mux_write(&mux, 1, line, sizeof(line)) == 1);
    assert(mux.windows[1].cells[0][0] == 'W');
    assert(mux.windows[1].cells[0][39] == 'W');
    assert(mux.windows[1].cells[1][0] == 'Z' && mux.windows[1].x == 1);
    assert(ss_mux_write(&mux, 1, (const uint8_t*)"\b", 1) == 0);
    assert(mux.windows[1].x == 0 && mux.windows[1].y == 1);
    assert(ss_mux_write(&mux, 1, (const uint8_t*)"\b", 1) == 1);
    assert(mux.windows[1].x == 39 && mux.windows[1].y == 0);
    assert(ss_mux_write(&mux, 1, (const uint8_t*)"B", 1) == 1);
    assert(mux.windows[1].cells[0][39] == 'B');
    assert(mux.windows[1].cells[1][0] == 'Z');
    assert(mux.windows[1].x == 0 && mux.windows[1].y == 1);
    assert(ss_mux_locate(&mux, 1, 0, 0) == 0);
    assert(ss_mux_write(&mux, 1, (const uint8_t*)"\b", 1) == 0);
    assert(mux.windows[1].x == 0 && mux.windows[1].y == 0);
    return 0;
}
