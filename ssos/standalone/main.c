#include "../os/kernel/kernel.h"

#include <unistd.h>
#include <x68k/iocs.h>

int main(void) {
    int old_ssp = _iocs_b_super(0);
    uint16_t saved_sr = ss_irq_save();
    if (ss_init() != SS_OK) {
        ss_irq_restore(saved_sr);
        (void)_iocs_b_super(old_ssp);
        _exit(1);
    }
    ss_set_interrupts();

    int status = ss_run();
    saved_sr = ss_irq_save();
    ss_restore_interrupts();
    ss_irq_restore(saved_sr);
    (void)_iocs_b_super(old_ssp);
    _exit(status);
}
