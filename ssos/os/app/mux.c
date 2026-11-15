#include "mux.h"

#include <stddef.h>

static void clear_windows(SSMux* mux) {
    mux->active = 0;
    for (unsigned n = 0; n < SS_MUX_WINDOWS; n++) {
        SSMuxWindow* window = &mux->windows[n];
        window->x = window->y = window->key_head = window->key_count = 0;
        window->scroll_count = 0;
        for (unsigned y = 0; y < SS_MUX_ROWS_MAX; y++)
            for (unsigned x = 0; x < SS_MUX_COLUMNS; x++)
                window->cells[y][x] = ' ';
    }
}

void ss_mux_init(SSMux* mux) {
    if (!mux) return;
    mux->rows = SS_MUX_ROWS;
    mux->columns = SS_MUX_COLUMNS;
    clear_windows(mux);
}

int ss_mux_set_rows(SSMux* mux, uint8_t rows) {
    if (!mux || !rows || rows > SS_MUX_ROWS_MAX) return -1;
    mux->rows = rows;
    clear_windows(mux);
    return 0;
}

int ss_mux_set_columns(SSMux* mux, uint8_t columns) {
    if (!mux || !columns || columns > SS_MUX_COLUMNS) return -1;
    mux->columns = columns;
    clear_windows(mux);
    return 0;
}

int ss_mux_region_decode(uint32_t start_offset, uint32_t d2, SSMuxRegion* out) {
    if (!out) return -1;
    uint32_t rows = (d2 & 0xffffu) + 1u;    /* d2.w = rows-1 */
    uint32_t columns = (d2 >> 16) + 1u;     /* d2.hw = columns-1 */
    /* Rows beyond the 31-row hardware text screen cannot be displayed; a
     * bogus report falls back to the known-good 25-row default. */
    if (!rows || rows > SS_MUX_ROWS_MAX) rows = SS_MUX_ROWS;
    if (!columns || columns > SS_MUX_COLUMNS) columns = SS_MUX_COLUMNS;
    out->rows = (uint8_t)rows;
    out->offset_supported = start_offset == 0;
    if (!out->offset_supported) columns = SS_MUX_COLUMNS;
    out->columns = (uint8_t)columns;
    return 0;
}

int ss_mux_select(SSMux* mux, uint8_t window) {
    if (!mux || window >= SS_MUX_WINDOWS) return -1;
    mux->active = window;
    return 0;
}

int ss_mux_clear(SSMux* mux, uint8_t window_id) {
    if (!mux || window_id >= SS_MUX_WINDOWS) return -1;
    SSMuxWindow* window = &mux->windows[window_id];
    for (unsigned y = 0; y < SS_MUX_ROWS_MAX; y++)
        for (unsigned x = 0; x < SS_MUX_COLUMNS; x++)
            window->cells[y][x] = ' ';
    window->x = window->y = 0;
    return 0;
}

int ss_mux_locate(SSMux* mux, uint8_t window_id, uint8_t x, uint8_t y) {
    if (!mux || window_id >= SS_MUX_WINDOWS || x >= mux->columns ||
        y >= mux->rows)
        return -1;
    mux->windows[window_id].x = x;
    mux->windows[window_id].y = y;
    return 0;
}

static void newline(SSMuxWindow* window, uint8_t rows) {
    if (++window->y < rows) return;
    window->scroll_count++;
    for (unsigned y = 1; y < rows; y++)
        for (unsigned x = 0; x < SS_MUX_COLUMNS; x++)
            window->cells[y - 1][x] = window->cells[y][x];
    for (unsigned x = 0; x < SS_MUX_COLUMNS; x++)
        window->cells[rows - 1][x] = ' ';
    window->y = rows - 1;
}

int ss_mux_write(SSMux* mux, uint8_t window_id, const uint8_t* data,
                 uint32_t size) {
    if (!mux || window_id >= SS_MUX_WINDOWS || (!data && size)) return -1;
    SSMuxWindow* window = &mux->windows[window_id];
    for (uint32_t i = 0; i < size; i++) {
        uint8_t c = data[i];
        if (c == '\r') window->x = 0;
        else if (c == '\n') newline(window, mux->rows);
        else if (c == '\b') {
            if (window->x) window->x--;
        } else if (c >= 32 && c != 127) {
            window->cells[window->y][window->x++] = c;
            if (window->x == mux->columns) {
                window->x = 0;
                newline(window, mux->rows);
            }
        }
    }
    return 0;
}

int ss_mux_key_push(SSMux* mux, uint16_t key) {
    if (!mux || mux->active >= SS_MUX_WINDOWS) return -1;
    SSMuxWindow* window = &mux->windows[mux->active];
    if (window->key_count == SS_MUX_KEY_CAPACITY) return -1;
    unsigned tail = (window->key_head + window->key_count) % SS_MUX_KEY_CAPACITY;
    window->keys[tail] = key;
    window->key_count++;
    return 0;
}

int ss_mux_key_pop(SSMux* mux, uint8_t window_id) {
    if (!mux || window_id >= SS_MUX_WINDOWS) return -1;
    SSMuxWindow* window = &mux->windows[window_id];
    if (!window->key_count) return -1;
    int key = window->keys[window->key_head];
    window->key_head = (window->key_head + 1u) % SS_MUX_KEY_CAPACITY;
    window->key_count--;
    return key;
}

int ss_mux_key_event(SSMux* mux, uint8_t* prefix, uint16_t event) {
    if (!mux || !prefix) return -1;
    uint8_t scan = (uint8_t)(event >> 8);
    uint8_t next = (uint8_t)event;
    if (scan >= 0x63 && scan <= 0x65) {
        *prefix = 0;
        uint8_t target = (uint8_t)(scan - 0x63);
        if (target == mux->active) return 0;
        return ss_mux_select(mux, target) == 0 ? 1 : -1;
    }
    if (!next) {
        if (scan) {
            if (*prefix && ss_mux_key_push(mux, *prefix) != 0) return -1;
            *prefix = 0;
            return ss_mux_key_push(mux, event);
        }
        return 0;
    }
    if (*prefix) {
        uint8_t old = *prefix;
        *prefix = 0;
        if (next >= '1' && next < '1' + SS_MUX_WINDOWS) {
            uint8_t target = (uint8_t)(next - '1');
            if (target == mux->active) return 0;
            return ss_mux_select(mux, target) == 0 ? 1 : -1;
        }
        if (ss_mux_key_push(mux, old) != 0) return -1;
        if (old == '~' && next == '~') return 0;
    }
    if (next == 0x1d || next == '~') { *prefix = next; return 0; }
    return ss_mux_key_push(mux, next);
}
