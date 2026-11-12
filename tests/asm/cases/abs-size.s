	.text
	move.l  0x4.w, %d0
	move.l  0x4.l, %d0
	move.l  0x20004, %d0
	move.w  0x1234.w, %d1
	move.w  %d0, 0x1236.w
	move.l  %d1, 0x20008
	movea.w 0x10.w, %a0
	movea.l 0x2000c, %a1
	lea     0x30.w, %a2
	lea     0x20010, %a3
	pea     0x40.w
	jsr     0x20014
	jmp     0x20018
movem.l 0x2001c, %d0-%d3
clr.w   0x50.w
clr.l   0x20020
tst.b   0x60.w
add.w   0x70.w, %d2
cmp.l   0x20024, %d3
