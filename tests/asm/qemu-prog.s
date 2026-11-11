.text
.globl _start

_start:
	moveq #7, %d0

loop:
	subq.l  #1, %d0
	bne.s   loop
	lea     (msg, %pc), %a0
	jsr     (putchar, %pc)
	rts
	.extern putchar
	.data

msg:
	.ascii "HI"
	.even
	.word  0
