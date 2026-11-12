	;       Linker negative sample (BADMAIN.O): references an extern that no input
	;       defines, so `ld` must stop with "Undefined symbol missing" and leave no
	;       output file.
	.text
	.globl  badmain
	.extern missing

badmain:
	bsr.w missing
	rts
