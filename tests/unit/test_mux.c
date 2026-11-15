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
    uint8_t prefix = 0;
    /* Ctrl-] (0x1d) is the other mux prefix; the emulated IOCS keyboard
     * cannot deliver it, so this unit path is its only coverage. */
    assert(ss_mux_key_event(&mux, &prefix, 0x1d) == 0 && prefix == 0x1d);
    /* active is window 1 already, so selecting 2 again is a no-op; then a
     * real switch back to window 1 (index 0) and a non-digit after the
     * prefix must deliver the prefix key followed by the key itself. */
    assert(ss_mux_key_event(&mux, &prefix, '2') == 0 && mux.active == 1);
    assert(prefix == 0);
    assert(ss_mux_key_event(&mux, &prefix, 0x1d) == 0 && prefix == 0x1d);
    assert(ss_mux_key_event(&mux, &prefix, '1') == 1 && mux.active == 0);
    assert(ss_mux_key_event(&mux, &prefix, 0x1d) == 0 && prefix == 0x1d);
    assert(ss_mux_key_event(&mux, &prefix, 'x') == 0 && prefix == 0);
    assert(ss_mux_key_pop(&mux, 0) == 0x1d);
    assert(ss_mux_key_pop(&mux, 0) == 'x');
    assert(ss_mux_key_event(&mux, &prefix, '~') == 0 && prefix == '~');
    assert(ss_mux_key_event(&mux, &prefix, '3') == 1 && mux.active == 2);
    assert(ss_mux_key_event(&mux, &prefix, '~') == 0);
    assert(ss_mux_key_event(&mux, &prefix, '~') == 0 && prefix == 0);
    assert(ss_mux_key_pop(&mux, 2) == '~');
    assert(ss_mux_key_event(&mux, &prefix, 0x6300) == 1 && mux.active == 0);
    for (unsigned i = 0; i < SS_MUX_KEY_CAPACITY; i++)
        assert(ss_mux_key_push(&mux, (uint8_t)i) == 0);
    assert(ss_mux_key_push(&mux, 0) == -1);
    /* A pending prefix plus a full queue: the prefix itself is retained
     * (a switch consumes no queue slot) but pushing it into a full queue
     * fails and the event is reported as an error. */
    assert(ss_mux_key_event(&mux, &prefix, '~') == 0 && prefix == '~');
    assert(ss_mux_key_event(&mux, &prefix, '3') == 1 && mux.active == 2);
    assert(prefix == 0);
    /* Back to the window whose queue is full, then a non-digit after the
     * prefix: delivering the prefix into the full queue must fail. */
    assert(ss_mux_key_event(&mux, &prefix, '~') == 0 && prefix == '~');
    assert(ss_mux_key_event(&mux, &prefix, '1') == 1 && mux.active == 0);
    assert(ss_mux_key_event(&mux, &prefix, '~') == 0 && prefix == '~');
    assert(ss_mux_key_event(&mux, &prefix, 'x') == -1);
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
    assert(ss_mux_write(&mux, 1, line, sizeof(line)) == 0);
    assert(mux.windows[1].cells[0][0] == 'W');
    assert(mux.windows[1].cells[0][39] == 'W');
    assert(mux.windows[1].cells[1][0] == 'Z' && mux.windows[1].x == 1);
    return 0;
}
