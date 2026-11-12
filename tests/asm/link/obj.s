	;      Linker sample: third object (OBJ.O). helper doubles the value in %d0 and
	;      reads its own data via A5 displacement. factor is global so MAIN.O can
	;      reference it across objects. The trailing pointer initial value to helper
	;      produces an ABS32 (data) relocation and one CODE32 fixup in the link.
	.text
	.globl helper

helper:
	add.l factor(%a5), %d0
	rts

	.data
	.globl factor

factor:
	.dc.l 10
	.dc.l helper
