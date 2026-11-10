#ifndef SS_MUX_H
#define SS_MUX_H

#include <stdint.h>

#define SS_MUX_WINDOWS 3u
#define SS_MUX_COLUMNS 80u
#define SS_MUX_ROWS 25u
#define SS_MUX_KEY_CAPACITY 64u

typedef struct {
    uint8_t cells[SS_MUX_ROWS][SS_MUX_COLUMNS];
    uint8_t keys[SS_MUX_KEY_CAPACITY];
    uint8_t x, y, key_head, key_count;
} SSMuxWindow;

typedef struct {
    SSMuxWindow windows[SS_MUX_WINDOWS];
    uint8_t active;
} SSMux;

/* Callers serialize access and perform physical display and keyboard I/O. */
void ss_mux_init(SSMux* mux);
int ss_mux_select(SSMux* mux, uint8_t window);
int ss_mux_write(SSMux* mux, uint8_t window, const uint8_t* data, uint32_t size);
int ss_mux_key_push(SSMux* mux, uint8_t key);
int ss_mux_key_pop(SSMux* mux, uint8_t window);
int ss_mux_clear(SSMux* mux, uint8_t window);

#endif
