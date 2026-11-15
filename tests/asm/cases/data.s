.text
.byte 1, 2, 3, 255
.even
.word 0x1234, -1
.long 0x12345678
.byte 7
.even
.word 9

foo:
	.word   foo
	.long   foo+4
	.equ    size, 16
	.word   size*2
	.byte   size>>2
	.byte   'Z
	.byte   'Z'
	.ascii  "AB"
	.asciz  "CD"
	.space  4
	.globl  foo
	.extern bar
	.long   bar
	.word   bar+2
	.data

msg:
	.byte  1, 2
	.even
	.word  4
	.bss
	.space 8
	.even
	.end
