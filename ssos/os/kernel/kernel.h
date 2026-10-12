#ifndef SS_KERNEL_H
#define SS_KERNEL_H

#include <stdint.h>

/* Error codes */
#define SS_OK          0
#define SS_ERR        (-1)
#define SS_ERR_PARAM  (-2)
#define SS_ERR_LIMIT  (-4)
#define SS_ERR_STATE  (-5)
#define SS_ERR_ID     (-6)

/* Task constants */
#define SS_MAX_TASKS   8
#define SS_MAX_PRI     16
#define SS_TASK_STACK  4096
#define SS_MIN_TASK_STACK 256

/* Context save levels */
#define SS_CTX_MINIMAL 0x00
#define SS_CTX_NORMAL  0x01
#define SS_CTX_FULL    0x02

/* Task states */
#define SS_TS_NONE     0
#define SS_TS_DORMANT  1
#define SS_TS_READY    2
#define SS_TS_WAIT     3
#define SS_TS_TERMINATED 4
#define SS_TS_INITIALIZING 5

/* Tick counter, incremented by the Timer D interrupt. */
extern volatile uint32_t ss_tick_counter;

/* Entry points */
void premain(void);
int16_t ss_init(void);
int ss_run(void);

/* Interrupt setup (defined in interrupts.s) */
void ss_set_interrupts(void);
void ss_restore_interrupts(void);
uint16_t ss_irq_save(void);
void ss_irq_restore(uint16_t saved_sr);

/* Linker symbols */
extern uint8_t __ssosstack_start;

#endif /* SS_KERNEL_H */
