.section .text
.align   2
.globl   ss_syscall_entry
.extern  ss_syscall_dispatch

	| trap #0 dispatcher. Preserve every register except D0, which returns the result.

ss_syscall_entry:
	movem.l d1-d7/a0-a6, -(sp)
	move.l  d1, -(sp)
	move.l  d0, -(sp)
	jsr     ss_syscall_dispatch
	addq.l  #8, sp
	movem.l (sp)+, d1-d7/a0-a6
	rte
