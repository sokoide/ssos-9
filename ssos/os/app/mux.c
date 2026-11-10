#include "mux.h"

#include <stddef.h>

void ss_mux_init(SSMux* mux) {
    if (!mux) return;
    mux->active = 0;
    for (unsigned n = 0; n < SS_MUX_WINDOWS; n++) {
        SSMuxWindow* window = &mux->windows[n];
        window->x = window->y = window->key_head = window->key_count = 0;
        for (unsigned y = 0; y < SS_MUX_ROWS; y++)
            for (unsigned x = 0; x < SS_MUX_COLUMNS; x++)
                window->cells[y][x] = ' ';
    }
}

int ss_mux_select(SSMux* mux, uint8_t window) {
    if (!mux || window >= SS_MUX_WINDOWS) return -1;
    mux->active = window;
    return 0;
}

int ss_mux_clear(SSMux* mux, uint8_t window_id) {
    if (!mux || window_id >= SS_MUX_WINDOWS) return -1;
    SSMuxWindow* window = &mux->windows[window_id];
    for (unsigned y = 0; y < SS_MUX_ROWS; y++)
        for (unsigned x = 0; x < SS_MUX_COLUMNS; x++)
            window->cells[y][x] = ' ';
    window->x = window->y = 0;
    return 0;
}

static void newline(SSMuxWindow* window) {
    if (++window->y < SS_MUX_ROWS) return;
    for (unsigned y = 1; y < SS_MUX_ROWS; y++)
        for (unsigned x = 0; x < SS_MUX_COLUMNS; x++)
            window->cells[y - 1][x] = window->cells[y][x];
    for (unsigned x = 0; x < SS_MUX_COLUMNS; x++)
        window->cells[SS_MUX_ROWS - 1][x] = ' ';
    window->y = SS_MUX_ROWS - 1;
}

int ss_mux_write(SSMux* mux, uint8_t window_id, const uint8_t* data,
                 uint32_t size) {
    if (!mux || window_id >= SS_MUX_WINDOWS || (!data && size)) return -1;
    SSMuxWindow* window = &mux->windows[window_id];
    for (uint32_t i = 0; i < size; i++) {
        uint8_t c = data[i];
        if (c == '\r') window->x = 0;
        else if (c == '\n') newline(window);
        else if (c == '\b') {
            if (window->x) window->x--;
        } else if (c >= 32 && c != 127) {
            window->cells[window->y][window->x++] = c;
            if (window->x == SS_MUX_COLUMNS) {
                window->x = 0;
                newline(window);
            }
        }
    }
    return 0;
}

int ss_mux_key_push(SSMux* mux, uint8_t key) {
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
