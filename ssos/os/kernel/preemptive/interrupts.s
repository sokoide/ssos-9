#include "scheduler_abi.h"

.section .text
.align   2
.globl   ss_set_interrupts, ss_restore_interrupts
.globl   ss_irq_save, ss_irq_restore
.globl   ss_tick_counter
.globl   ss_save_data_base
.globl   ss_task_yield
.globl   ss_context_switch_count
.type    ss_set_interrupts, @function

ss_irq_save:
	move.w %sr, %d0
	ori.w  #0x0700, %sr
	rts

ss_irq_restore:
	move.l 4(%sp), %d0
	move.w %d0, %sr
	rts

	| ============================================================
	| ss_set_interrupts - Initialize MFP and interrupt vectors
	| ============================================================

ss_set_interrupts:
	movem.l d2-d7/a2-a6, -(sp)

	|      Disable interrupts
	move.w #0x2700, %sr

	|   Save original interrupt state
	jsr save_interrupts

	|      MFP registers: VR=$E88017, IERB=$E88009, ISRB=$E88011
	|      IMRB=$E88015, TCDCR=$E8801D, TDDR=$E88025.
	|      Vector = VR base + channel: GPIP7 is $40+$0F=$4F. Keep the
	|      current EOI mode and all Human68k source enable/masks except
	|      Timer D (IERB/IMRB bit 4), so unrelated sources cannot reach us.
	move.b 0xe88017, d0
	andi.b #0x08, d0
	ori.b  #0x40, d0
	move.b d0, 0xe88017
	move.b 0xe88009, d0
	andi.b #0xef, d0
	move.b d0, 0xe88009
	move.b #0xef, 0xe88011

	|      Install the Timer D handler.
	lea    ss_timerd_handler, a0
	move.l a0, 0x110

	|      Stop and configure Timer D only; preserve Timer C's mode in
	|      TCDCR's upper nibble. Low bits select Timer D prescaling.
	move.b 0xe8801d, d0
	andi.b #0xf0, d0
	move.b d0, 0xe8801d

	|      TDDR=$E88025. Timer D: 4MHz / 200 / 100 = 200Hz (5ms).
	move.b #100, 0xe88025
	ori.b  #0x07, d0
	move.b d0, 0xe8801d

	|      Enable Timer D in IERB and unmask it in IMRB. Preserve every
	|      other channel; IMRB bit 4 is the Timer D mask bit.
	move.b 0xe88009, d0
	ori.b  #0x10, d0
	move.b d0, 0xe88009
	move.b 0xe88015, d0
	ori.b  #0x10, d0
	move.b d0, 0xe88015

	|      Supervisor mode, IPL mask 0: allow interrupt levels 1-7.
	move.w #0x2000, %sr

	movem.l (sp)+, d2-d7/a2-a6
	rts

	|      ============================================================
	|      Interrupt handlers
	|      ============================================================
	.globl ss_timerd_handler

	|       ============================================================
	|       TimerD handler - 5ms tick; switch every 10 ticks
	|
	|       TCB offsets shared with the C layout in scheduler_abi.h.
	|
	|       resume_type: 0 = timer-interrupted (rte safe)
	|       1 = yielded (manual SR/PC restore)
	|       ============================================================
	.extern ss_curr_task
	.extern ss_scheduled_task
	.extern ss_do_context_switch
	.extern ss_do_wakeups

ss_timerd_handler:
	move.w  #0x2700, %sr  | Disable interrupts to prevent nesting
	|       Minimal save: only d0/a0 needed for non-switch path
	movem.l d0/a0, -(sp)
	addq.l  #1, ss_tick_counter

	lea    ss_switch_tick, a0
	addq.b #1, (a0)
	cmpi.b #10, (a0)
	bne.s  .no_switch

	|       Switch tick: restore minimal, do full save
	movem.l (sp)+, d0/a0
	movem.l d0-d7/a0-a6, -(sp)
	lea     ss_switch_tick, a0
	move.b  #0, (a0)
	addq.l  #1, ss_context_switch_count

	bsr ss_do_wakeups

	move.l ss_curr_task, d0
	beq.s  .no_switch_full

	|      Reset ISRB Timer D bit (clear bit 4)
	move.l #0xe88011, a0
	move.b (a0), d1
	andi.b #0xef, d1
	move.b d1, (a0)

	|      Mark as timer-interrupted (resume_type = 0)
	move.l ss_curr_task, a1
	move.b #0, SS_TCB_RESUME_TYPE_OFFSET(a1)

	bra.w ss_context_switch

.no_switch:
	move.l  #0xe88011, a0
	move.b  (a0), d0
	andi.b  #0xef, d0
	move.b  d0, (a0)
	movem.l (sp)+, d0/a0
	rte

.no_switch_full:
	move.l  #0xe88011, a0
	move.b  (a0), d0
	andi.b  #0xef, d0
	move.b  d0, (a0)
	movem.l (sp)+, d0-d7/a0-a6
	rte

	| ============================================================
	| ss_context_switch - Save current task, switch to next
	|
	| On entry: all registers saved on current task's stack
	| resume_type already set in TCB
	| ============================================================

ss_context_switch:
	|      Save current SP to curr_task->context
	move.l ss_curr_task, a1
	move.l sp, SS_TCB_CONTEXT_OFFSET(a1)

	|   Call C scheduler to pick next task
	bsr ss_do_context_switch

	| Fall through to .resume_task

	| ============================================================
	| .resume_task - Resume the scheduled task
	| Uses the shared resume_type offset to select restore method
	| ============================================================

.resume_task:
	move.l ss_scheduled_task, a1
	move.l SS_TCB_CONTEXT_OFFSET(a1), sp

	|      Check if this is a new (never-run) task.  A created task has
	|      context == stack_base and a non-NULL entry.  The bootstrap main
	|      task uses the same stack sentinel but deliberately has no entry.
	move.l SS_TCB_CONTEXT_OFFSET(a1), a0
	move.l SS_TCB_STACK_BASE_OFFSET(a1), d0
	cmp.l  a0, d0
	bne.s  .resume_existing
	tst.l  SS_TCB_ENTRY_OFFSET(a1)
	bne.w  .start_task

.resume_existing:

	|      Check resume_type at its shared TCB offset
	cmpi.b #0, SS_TCB_RESUME_TYPE_OFFSET(a1)
	beq.s  .resume_interrupted

	|       resume_type == 1: yielded, manual SR/PC restore (no rte)
	movem.l (sp)+, d0-d7/a0-a6
	move.l  (sp)+, %a0
	move.w  (sp)+, %sr
	jmp     (%a0)

.resume_interrupted:
	|       resume_type == 0: timer-interrupted, CPU frame intact, rte safe
	movem.l (sp)+, d0-d7/a0-a6
	rte

.start_task:
	|      New task: call the C entry with arg; returning ends the task.
	move.l SS_TCB_ENTRY_OFFSET(a1), a0  | a0 = task entry function
	move.l SS_TCB_ARG_OFFSET(a1), -(sp)  | entry(arg)
	move.w #0x2000, %sr  | enable interrupts
	jsr    (%a0)
	addq.l #SS_FRAME_ARG_BYTES, sp
	jmp    ss_task_exit

	| ============================================================
	| ss_task_yield - Voluntary context switch (callable from C)
	| ============================================================

ss_task_yield:
	|       Keep the original SR in the frame, then mask Timer D before
	|       touching resume_type or context.  Otherwise an interrupt can
	|       replace the yielded frame with an rte frame between those writes.
	move.w  %sr, -(sp)
	ori.w   #0x0700, %sr
	pea     .yield_resume
	|       Save all registers
	movem.l d0-d7/a0-a6, -(sp)
	|       Mark as yielded (resume_type = 1)
	move.l  ss_curr_task, a1
	move.b  #1, SS_TCB_RESUME_TYPE_OFFSET(a1)
	|       Save SP and switch
	move.l  sp, SS_TCB_CONTEXT_OFFSET(a1)
	bsr     ss_do_context_switch
	bra.w   .resume_task

.yield_resume:
	rts

	|        ============================================================
	|        Data section
	|        ============================================================
	.section .data
	.even

ss_tick_counter:
	dc.l 0

ss_switch_tick:
	dc.b 0
	.even

ss_context_switch_count:
	dc.l 0

	.section .bss
	.even

ss_save_data_base:
	ds.b 1024
