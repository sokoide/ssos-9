#include "cli_console.h"

#include "../kernel/scheduler.h"

#include <x68k/iocs.h>

static uint8_t console_busy;

static void console_lock(void) {
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

static void console_unlock(void) {
    uint16_t saved_sr = ss_irq_save();
    console_busy = 0;
    ss_irq_restore(saved_sr);
}

/* Background builtins hold the process guard across command execution, so
 * cancellation cannot reclaim a stack while it owns this IOCS lock. */
void ss_cli_console_print(const char* text) {
    console_lock();
    _iocs_b_print(text);
    console_unlock();
}

void ss_cli_console_clear(void) {
    console_lock();
    _iocs_b_clr_st();
    _iocs_b_locate(0, 0);
    console_unlock();
}

int ss_cli_console_read_key(void) {
    console_lock();
    int key = _iocs_b_keysns() > 0 ? (_iocs_b_keyinp() & 0xff) : -1;
    console_unlock();
    return key;
}
