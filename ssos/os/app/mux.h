#ifndef SS_MUX_H
#define SS_MUX_H

#include <stdint.h>

#define SS_MUX_WINDOWS 3u
#define SS_MUX_COLUMNS 80u
#define SS_MUX_ROWS 25u          /* default height when the region is unknown */
#define SS_MUX_ROWS_MAX 31u      /* hardware text screen rows above the FK row */
#define SS_MUX_KEY_CAPACITY 64u

typedef struct {
    uint8_t cells[SS_MUX_ROWS_MAX][SS_MUX_COLUMNS];
    uint16_t keys[SS_MUX_KEY_CAPACITY];
    uint32_t scroll_count;
    uint8_t x, y, key_head, key_count;
} SSMuxWindow;

typedef struct {
    SSMuxWindow windows[SS_MUX_WINDOWS];
    uint8_t active;
    uint8_t rows;    /* active window height, [1, SS_MUX_ROWS_MAX] */
    uint8_t columns; /* active window width, [1, SS_MUX_COLUMNS] */
} SSMux;

/* Decoded IOCS B_CONSOL region: d2.hw = columns-1, d2.w = rows-1,
 * d1.l = display start offset (X/8+Y*128). */
typedef struct {
    uint8_t rows;
    uint8_t columns;
    uint8_t offset_supported; /* 0 when the start offset is nonzero */
} SSMuxRegion;

/* Callers serialize access and perform physical display and keyboard I/O. */
void ss_mux_init(SSMux* mux);
/* Change the window height to match the IOCS console region. Clears every
 * window, so call it before any output (at enable time). */
int ss_mux_set_rows(SSMux* mux, uint8_t rows);
/* Change the window width to match the IOCS console region. Same clearing
 * rule as ss_mux_set_rows. */
int ss_mux_set_columns(SSMux* mux, uint8_t columns);
/* Decode a B_CONSOL result for multiplexer use. Rows above the 31-row text
 * screen fall back to the SS_MUX_ROWS default; the width is clamped to
 * SS_MUX_COLUMNS. A nonzero start offset is unsupported by the mux drawing
 * model (it addresses the screen from absolute 0,0): offset_supported is 0
 * and the reported width is ignored so the caller can warn and keep the
 * fixed width. */
int ss_mux_region_decode(uint32_t start_offset, uint32_t d2, SSMuxRegion* out);
int ss_mux_select(SSMux* mux, uint8_t window);
/* Returns 1 if a printable character or backspace crossed a column
 * boundary, 0 for other writes, or -1 for invalid input. */
int ss_mux_write(SSMux* mux, uint8_t window, const uint8_t* data, uint32_t size);
int ss_mux_key_push(SSMux* mux, uint16_t key);
int ss_mux_key_pop(SSMux* mux, uint8_t window);
/* Feed one IOCS key event; returns 1 when the active window changes. */
int ss_mux_key_event(SSMux* mux, uint16_t event);
int ss_mux_clear(SSMux* mux, uint8_t window);
int ss_mux_locate(SSMux* mux, uint8_t window, uint8_t x, uint8_t y);

#endif
