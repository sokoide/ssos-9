.text
.globl helper

helper:
	nop
	bsr.w  leaf
	addq.l #1, %d0
	rts

leaf:
	moveq #2, %d0
	rts
	nop
