	;       Linker sample: main object (MAIN.O). Exercises local and external A5
	;       displaced data access (ABS16 folding to private data offsets), an external
	;       PC16 call, a local PC16 reference to a text constant, and bss readback.
	;       Returns base_val(7) + factor(10) doubled by helper = 27.
	.text
	.globl  main
	.extern helper
	.extern factor

main:
	move.l base_val(%a5), %d0
	add.l  factor(%a5), %d0
	bsr.w  helper
	move.l %d0, result(%a5)
	lea    msg(%pc), %a0
	move.l %a0, wt_buf(%a5)
	lea    wt(%a5), %a1
	move.l %a1, %d1
	moveq  #35, %d0
	trap   #0
	move.l result(%a5), %d0
	rts

msg:
	.ascii "LNK\n"

	.data

base_val:
	.dc.l 7

wt:
	.dc.l 1

wt_buf:
	.dc.l 0

wt_len:
	.dc.l 4

	.bss

result:
	.space 4
