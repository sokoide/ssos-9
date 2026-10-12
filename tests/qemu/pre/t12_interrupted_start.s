.section .text
.align   2
.globl   ss_process_frame_entry
.extern  seen_d0
.extern  seen_a0
.extern  seen_a3
.extern  seen_a5
.extern  ss_task_exit

ss_process_frame_entry:
	move.l d0, seen_d0
	move.l a0, seen_a0
	move.l a3, seen_a3
	move.l a5, seen_a5
	rts
