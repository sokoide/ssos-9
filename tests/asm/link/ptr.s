	;      Linker sample: fourth object (PTR.O). Data pointers to labels that are
	;      not at a section start, and externals with constant addends, exercise
	;      the addend folding in the CODE32/DATA32 fixup payloads.
	.text
first:
	nop
second:
	rts
	.data
	.dc.l second
	.dc.l first+4
	.dc.l tail+4
mid:
	.dc.l mid+8
	.extern factor
	.dc.l factor+4
	.bss
tail:
	.ds.b 8
