.include "iocscall.mac"

.section .text
.align   2
.globl   entry
.type    entry, @function

entry:
	|      Set supervisor stack pointer
	move.l #0x010000, a0
	move.l a0, sp
	|      Keep interrupts masked until premain initializes every scheduler queue.
	move.w #0x2700, %sr
	clr.l  -(sp)

	|   Jump to C initialization
	jmp premain

.end entry
