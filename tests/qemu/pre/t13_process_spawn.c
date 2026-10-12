/* Run one shared S9M1 code image with independent A5/GOT state. */

#include "builtin.h"
#include "main_task.h"
#include "process.h"
#include "system_arena.h"
#include "tty.h"

static SSTask main_tcb;
static uint8_t image[124];

static void put16(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put32(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint32_t get32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t crc32(void) {
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0; i < sizeof(image); i++) {
        uint8_t value = i >= 0x34 && i < 0x38 ? 0 : image[i];
        crc ^= value;
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0);
    }
    return crc ^ 0xffffffffu;
}

static void make_image(void) {
    image[0] = 'S';
    image[1] = '9';
    image[2] = 'M';
    image[3] = '1';
    put16(&image[4], 1);
    put16(&image[6], SS_MODULE_IMAGE_HEADER_SIZE);
    put32(&image[8], SS_MODULE_FLAG_PIC | SS_MODULE_FLAG_REENTRANT);
    put32(&image[0x0c], sizeof(image));
    put32(&image[0x10], 80);
    put32(&image[0x14], 8);
    put32(&image[0x18], 80);
    put32(&image[0x1c], 88);
    put32(&image[0x20], 16);
    put32(&image[0x24], 4);
    put32(&image[0x28], 4096);
    put32(&image[0x2c], 104);
    put16(&image[0x30], 8);
    put32(&image[0x38], 0);
    put32(&image[0x3c], 4);
    put32(&image[0x40], 112);
    put32(&image[0x44], 1);
    for (unsigned i = 0; i < 8; i++) image[104 + i] = (uint8_t)"qcounter"[i];

    /* movea.l (a5),a0; addq.l #1,(a0); trap #0; bra.s back to start. */
    image[80] = 0x20;
    image[81] = 0x55;
    image[82] = 0x52;
    image[83] = 0x90;
    image[84] = 0x4e;
    image[85] = 0x40;
    image[86] = 0x60;
    image[87] = 0xf8;
    put32(&image[112], 0);
    put32(&image[116], SS_MODULE_FIXUP_DATA32);
    put32(&image[120], 8);
    put32(&image[0x34], crc32());
}

static void fail(const char* reason) {
    tty_puts("FAIL ");
    tty_puts(reason);
    tty_puts("\n");
    for (;;) __asm__ volatile("trap #0" ::: "memory");
}

static void tick(void) { __asm__ volatile("trap #0" ::: "memory"); }

static void set_counter(void* data, uint32_t value) {
    put32((uint8_t*)data + 8, value);
}

static void run_ticks(unsigned count) {
    for (unsigned i = 0; i < count; i++) tick();
}

int main(void) {
    tty_puts("START process-spawn\n");
    if (ss_memory_init() != SS_OK)
        fail("memory init");
    ss_module_registry_init();
    if (ss_builtin_modules_init() != SS_OK)
        fail("built-in modules");
    ss_sched_init();
    if (ss_main_task_register(&main_tcb, 8) != SS_OK ||
        ss_idle_task_start() != SS_OK || ss_reaper_task_start() != SS_OK ||
        ss_process_init(&main_tcb) != SS_OK)
        fail("kernel tables init");

    make_image();
    SSModuleId module_id;
    if (ss_module_register_image(image, sizeof(image),
                                 SS_MODULE_STORAGE_BUILTIN,
                                 &module_id) != SS_OK)
        fail("module register");

    const char* argv[] = {"qcounter"};
    SSProcessId first_pid;
    SSProcessId second_pid;
    uint32_t free_before = ss_memory_free_bytes();
    if (ss_process_spawn(1, module_id, 1, argv, &first_pid) != SS_OK ||
        ss_process_spawn(1, module_id, 1, argv, &second_pid) != SS_OK)
        fail("spawn pair");
    SSProcessSnapshot first;
    SSProcessSnapshot second;
    if (ss_process_snapshot(first_pid, &first) != SS_OK ||
        ss_process_snapshot(second_pid, &second) != SS_OK ||
        first.module_id != second.module_id || first.module_id != module_id ||
        first.data_base == second.data_base ||
        first.got_base == second.got_base ||
        first.stack_low == second.stack_low)
        fail("private regions not separated");

    set_counter(first.data_base, 1000);
    if (get32(second.data_base + 8) != 0)
        fail("data area aliases");
    run_ticks(30);
    if (get32(first.data_base + 8) <= 1000 || get32(second.data_base + 8) == 0)
        fail("shared PIC failed to use private GOT");
    if (ss_process_snapshot(first_pid, &first) != SS_OK)
        fail("first snapshot after run");
    if (ss_process_snapshot(second_pid, &second) != SS_OK ||
        first.state != SS_PROCESS_READY || second.state != SS_PROCESS_READY)
        fail("running process was reaped");

    uint16_t status;
    if (ss_process_kill(first_pid) != SS_OK)
        fail("kill first");
    int16_t wait_status = SS_ERR_STATE;
    for (unsigned i = 0; i < 32 && wait_status == SS_ERR_STATE; i++) {
        wait_status = ss_process_wait(1, first_pid, &status);
        if (wait_status == SS_ERR_STATE)
            tick();
    }
    if (wait_status != SS_OK || status != 0)
        fail("reap/wait first");
    if (ss_process_kill(second_pid) != SS_OK)
        fail("kill second");
    wait_status = SS_ERR_STATE;
    for (unsigned i = 0; i < 32 && wait_status == SS_ERR_STATE; i++) {
        wait_status = ss_process_wait(1, second_pid, &status);
        if (wait_status == SS_ERR_STATE)
            tick();
    }
    if (wait_status != SS_OK || status != 0 ||
        ss_memory_free_bytes() != free_before)
        fail("reap/wait second or RAM leak");

    tty_puts("OK shared code/private A5 data\n");
    for (;;) tick();
}
