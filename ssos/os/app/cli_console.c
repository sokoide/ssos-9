#include "cli_console.h"

#include "../kernel/scheduler.h"
#include "../process/process.h"
#include "mux.h"

#include <x68k/iocs.h>

static uint8_t console_busy;
static uint8_t mux_enabled;
static SSMux mux;

static void redraw(void) {
    const SSMuxWindow* window = &mux.windows[mux.active];
    _iocs_b_clr_st();
    for (unsigned y = 0; y < mux.rows; y++) {
        unsigned end = mux.columns;
        while (end && window->cells[y][end - 1] == ' ') end--;
        /* Avoid a physical terminal scroll when writing the bottom-right
         * cell. It remains in the backing buffer until the next redraw. */
        if (y == mux.rows - 1 && end == mux.columns) end--;
        if (!end) continue;
        _iocs_b_locate(0, (int)y);
        for (unsigned x = 0; x < end; x++)
            (void)_iocs_b_putc(window->cells[y][x]);
    }
    _iocs_b_locate(window->x, window->y);
}

#if defined(SS_HOST_TEST) || defined(SS_QEMU_TEST)
uint32_t ss_test_console_offset;
uint32_t ss_test_console_d2;
#endif

/* The hardware text screen shows 25 rows in 15 kHz modes and 31 rows above
 * the function-key row in 31 kHz modes. B_CONSOL with d1=d2=-1 returns the
 * live console region (d1.l start offset X/8+Y*128, d2: high word
 * columns-1, low word rows-1), so the multiplexer windows fill the visible
 * region instead of stopping at the 25-row, 80-column default. Returns 0
 * with the raw result when a region is reported; -1 keeps the defaults
 * (host harnesses without an ss_test_console_* override). */
static int console_region_query(uint32_t* offset, uint32_t* d2) {
#if defined(SS_HOST_TEST) || defined(SS_QEMU_TEST)
    if (!ss_test_console_offset && !ss_test_console_d2) return -1;
    *offset = ss_test_console_offset;
    *d2 = ss_test_console_d2;
    return 0;
#else
    register int32_t d0r __asm__("d0") = 0x2e; /* IOCS B_CONSOL */
    register int32_t d1r __asm__("d1") = -1;
    register int32_t d2r __asm__("d2") = -1;
    __asm__ volatile("trap #15" : "+d"(d0r), "+d"(d1r), "+d"(d2r)
                             : : "cc", "memory");
    *offset = (uint32_t)d1r;
    *d2 = (uint32_t)d2r;
    return 0;
#endif
}

void ss_cli_console_mux_enable(void) {
    SSMuxRegion region = {SS_MUX_ROWS, SS_MUX_COLUMNS, 1};
    ss_iocs_lock();
    uint32_t offset = 0, d2 = 0;
    if (console_region_query(&offset, &d2) == 0)
        (void)ss_mux_region_decode(offset, d2, &region);
    ss_mux_init(&mux);
    (void)ss_mux_set_rows(&mux, region.rows);
    (void)ss_mux_set_columns(&mux, region.columns);
    mux_enabled = 1;
    redraw();
    ss_iocs_unlock();
    /* The mux drawing model addresses the screen from absolute 0,0, so a
     * nonzero display start offset cannot be honored: the width stays at
     * the SS_MUX_COLUMNS default (rows remain variable) and one line says
     * so through the regular print path. */
    if (!region.offset_supported)
        ss_cli_console_print("mux: display start offset unsupported\r\n");
}

uint8_t ss_cli_console_mux_active(void) {
    uint16_t saved = ss_irq_save();
    uint8_t result = mux_enabled ? mux.active : 0;
    ss_irq_restore(saved);
    return result;
}

int ss_cli_console_mux_select(uint8_t window) {
    ss_iocs_lock();
    int result = mux_enabled ? ss_mux_select(&mux, window) : -1;
    if (result == 0) redraw();
    ss_iocs_unlock();
    return result;
}

void ss_iocs_lock(void) {
    for (;;) {
        uint16_t saved_sr = ss_irq_save();
        if (!console_busy) {
            console_busy = 1;
            ss_irq_restore(saved_sr);
            return;
        }
        ss_irq_restore(saved_sr);
        (void)ss_task_sleep(1);
    }
}

void ss_iocs_unlock(void) {
    uint16_t saved_sr = ss_irq_save();
    console_busy = 0;
    ss_irq_restore(saved_sr);
}

/* Background builtins hold the process guard across command execution, so
 * cancellation cannot reclaim a stack while it owns this IOCS lock. */
void ss_cli_console_print(const char* text) {
    ss_iocs_lock();
    if (!mux_enabled) _iocs_b_print(text);
    else {
        uint32_t size = 0;
        while (text[size]) size++;
        uint8_t id = ss_process_current_console();
        uint32_t old_scroll = mux.windows[id].scroll_count;
        int wrapped = ss_mux_write(&mux, id, (const uint8_t*)text, size);
        if (id == mux.active) {
            if (wrapped || mux.windows[id].scroll_count != old_scroll) redraw();
            else _iocs_b_print(text);
        }
    }
    ss_iocs_unlock();
}

void ss_cli_console_write(const uint8_t* bytes, uint32_t length) {
    ss_iocs_lock();
    uint8_t id = mux_enabled ? ss_process_current_console() : 0;
    uint32_t old_scroll = mux_enabled ? mux.windows[id].scroll_count : 0;
    int wrapped = mux_enabled ? ss_mux_write(&mux, id, bytes, length) : 0;
    if (mux_enabled && id == mux.active &&
        (wrapped || mux.windows[id].scroll_count != old_scroll))
        redraw();
    else if (!mux_enabled || id == mux.active)
        for (uint32_t i = 0; i < length; i++)
            (void)_iocs_b_putc(bytes[i]);
    ss_iocs_unlock();
}

void ss_cli_console_clear(void) {
    ss_iocs_lock();
    uint8_t id = mux_enabled ? ss_process_current_console() : 0;
    if (mux_enabled) (void)ss_mux_clear(&mux, id);
    if (!mux_enabled || id == mux.active) {
        _iocs_b_clr_st();
        _iocs_b_locate(0, 0);
    }
    ss_iocs_unlock();
}

uint32_t ss_cli_console_size(void) {
    ss_iocs_lock();
    uint32_t columns = SS_MUX_COLUMNS;
    uint8_t rows;
    if (mux_enabled) {
        columns = mux.columns;
        rows = mux.rows;
    } else {
        /* Without the multiplexer only the height follows the live region;
         * the width stays at the multiplexer default. */
        uint32_t offset, d2;
        SSMuxRegion region = {SS_MUX_ROWS, SS_MUX_COLUMNS, 1};
        if (console_region_query(&offset, &d2) == 0)
            (void)ss_mux_region_decode(offset, d2, &region);
        rows = region.rows;
    }
    ss_iocs_unlock();
    return (columns << 16) | rows;
}

int ss_cli_console_locate(uint32_t x, uint32_t y) {
    /* Without the multiplexer the bound is the tallest hardware text screen;
     * IOCS clips to the live console region. Under the multiplexer the
     * buffer bound is the clamped region width (ss_mux_locate). */
    if (x >= SS_MUX_COLUMNS || y >= SS_MUX_ROWS_MAX) return -1;
    ss_iocs_lock();
    uint8_t id = mux_enabled ? ss_process_current_console() : 0;
    if (mux_enabled) (void)ss_mux_locate(&mux, id, (uint8_t)x, (uint8_t)y);
    if (!mux_enabled || id == mux.active) _iocs_b_locate((int)x, (int)y);
    ss_iocs_unlock();
    return 0;
}

int ss_cli_console_read_key(void) {
    ss_iocs_lock();
    int key = -1;
    if (!mux_enabled) {
        if (_iocs_b_keysns() > 0) {
            uint16_t event = (uint16_t)_iocs_b_keyinp();
            key = (event & 0xffu) ? (event & 0xffu) : event;
        }
    } else {
        while (_iocs_b_keysns() > 0) {
            if (mux.windows[mux.active].key_count >=
                SS_MUX_KEY_CAPACITY) break;
            int event_result = ss_mux_key_event(&mux,
                                                (uint16_t)_iocs_b_keyinp());
            if (event_result < 0) break;
            if (event_result > 0) redraw();
        }
        key = ss_mux_key_pop(&mux, ss_process_current_console());
    }
    ss_iocs_unlock();
    return key;
}
