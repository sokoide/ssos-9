.text
.globl  _start
.extern helper

_start:
	bsr.w helper
	jsr   helper(%pc)
	nop
	bra.w fin
	nop

fin:
	trap #0
	nop
