.section .text
.align   2
.globl   ss_syscall_entry
.extern  ss_syscall_dispatch
.extern  ss_process_syscall_enter
.extern  ss_process_syscall_leave
.extern  ss_task_exit

	| trap #0 dispatcher. Preserve every register except D0, which returns the result.

ss_syscall_entry:
	move.w  #0x2700, sr
	movem.l d1-d7/a0-a6, -(sp)
	move.l  d1, -(sp)
	move.l  d0, -(sp)
	jsr     ss_process_syscall_enter
	move.w  #0x2000, sr
	jsr     ss_syscall_dispatch
	addq.l  #8, sp
	move.l  d0, -(sp)
	move.w  #0x2700, sr
	jsr     ss_process_syscall_leave
	tst.l   d0
	bne.s   .kill_current
	move.l  (sp)+, d0
	move.w  #0x2000, sr
	movem.l (sp)+, d1-d7/a0-a6
	rte

.kill_current:
	jsr   ss_task_exit
	bra.s .kill_current
