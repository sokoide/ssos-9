#include "cli_console.h"

#include "../kernel/scheduler.h"
#include "../process/process.h"
#include "mux.h"

#include <x68k/iocs.h>

static uint8_t console_busy;
static uint8_t mux_enabled;
static uint8_t mux_prefix;
static SSMux mux;

static void redraw(void) {
    const SSMuxWindow* window = &mux.windows[mux.active];
    _iocs_b_clr_st();
    for (unsigned y = 0; y < mux.rows; y++) {
        unsigned end = SS_MUX_COLUMNS;
        while (end && window->cells[y][end - 1] == ' ') end--;
        /* Avoid a physical terminal scroll when writing the bottom-right
         * cell. It remains in the backing buffer until the next redraw. */
        if (y == mux.rows - 1 && end == SS_MUX_COLUMNS) end--;
        if (!end) continue;
        _iocs_b_locate(0, (int)y);
        for (unsigned x = 0; x < end; x++)
            (void)_iocs_b_putc(window->cells[y][x]);
    }
    _iocs_b_locate(window->x, window->y);
}

/* The hardware text screen shows 25 rows in 15 kHz modes and 31 rows above
 * the function-key row in 31 kHz modes. B_CONSOL with d1=d2=-1 returns the
 * live console region (d2: high word columns-1, low word rows-1), so the
 * multiplexer windows fill the whole visible screen instead of stopping at
 * the 25-row default. Synthetic harnesses keep the default. */
static uint8_t console_region_rows(void) {
#if defined(SS_HOST_TEST) || defined(SS_QEMU_TEST)
    return SS_MUX_ROWS;
#else
    register int32_t d0 __asm__("d0") = 0x2e; /* IOCS B_CONSOL */
    register int32_t d1 __asm__("d1") = -1;
    register int32_t d2 __asm__("d2") = -1;
    __asm__ volatile("trap #15" : "+d"(d0), "+d"(d1), "+d"(d2)
                             : : "cc", "memory");
    uint32_t rows = ((uint32_t)(uint16_t)d2 & 0xffffu) + 1u;
    if (!rows || rows > SS_MUX_ROWS_MAX) return SS_MUX_ROWS;
    return (uint8_t)rows;
#endif
}

void ss_cli_console_mux_enable(void) {
    ss_iocs_lock();
    ss_mux_init(&mux);
    (void)ss_mux_set_rows(&mux, console_region_rows());
    mux_prefix = 0;
    mux_enabled = 1;
    redraw();
    ss_iocs_unlock();
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
        (void)ss_mux_write(&mux, id, (const uint8_t*)text, size);
        if (id == mux.active) {
            if (mux.windows[id].scroll_count != old_scroll) redraw();
            else _iocs_b_print(text);
        }
    }
    ss_iocs_unlock();
}

void ss_cli_console_write(const uint8_t* bytes, uint32_t length) {
    ss_iocs_lock();
    uint8_t id = mux_enabled ? ss_process_current_console() : 0;
    uint32_t old_scroll = mux_enabled ? mux.windows[id].scroll_count : 0;
    if (mux_enabled) (void)ss_mux_write(&mux, id, bytes, length);
    if (mux_enabled && id == mux.active && mux.windows[id].scroll_count != old_scroll)
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
    uint8_t rows = mux_enabled ? mux.rows : console_region_rows();
    ss_iocs_unlock();
    return ((uint32_t)SS_MUX_COLUMNS << 16) | rows;
}

int ss_cli_console_locate(uint32_t x, uint32_t y) {
    /* Without the multiplexer the bound is the tallest hardware text screen;
     * IOCS clips to the live console region. */
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
                SS_MUX_KEY_CAPACITY - (mux_prefix ? 1u : 0u)) break;
            int event_result = ss_mux_key_event(&mux, &mux_prefix,
                                                (uint16_t)_iocs_b_keyinp());
            if (event_result < 0) break;
            if (event_result > 0) redraw();
        }
        key = ss_mux_key_pop(&mux, ss_process_current_console());
    }
    ss_iocs_unlock();
    return key;
}
