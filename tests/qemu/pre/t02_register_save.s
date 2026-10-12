.section .text
.align   2
.globl   ss_test_register_roundtrip
.extern  ss_register_failure

ss_test_register_roundtrip:
	movem.l d2-d7/a2-a6, -(sp)
	move.l  #0x10203040, d0
	move.l  #0x21314151, d1
	move.l  #0x32425262, d2
	move.l  #0x43536373, d3
	move.l  #0x54647484, d4
	move.l  #0x65758595, d5
	move.l  #0x768696a6, d6
	move.l  #0x8797a7b7, d7
	movea.l #0x98a8b8c8, a0
	movea.l #0xa9b9c9d9, a1
	movea.l #0xbacadaea, a2
	movea.l #0xcbdbebfb, a3
	movea.l #0xdcedfc0c, a4
	movea.l #0xedfd0d1d, a5
	movea.l #0xfe0e1e2e, a6
	trap    #1
	cmpi.l  #0x10203040, d0
	bne.s   .failed
	cmpi.l  #0x21314151, d1
	bne.s   .failed
	cmpi.l  #0x32425262, d2
	bne.s   .failed
	cmpi.l  #0x43536373, d3
	bne.s   .failed
	cmpi.l  #0x54647484, d4
	bne.s   .failed
	cmpi.l  #0x65758595, d5
	bne.s   .failed
	cmpi.l  #0x768696a6, d6
	bne.s   .failed
	cmpi.l  #0x8797a7b7, d7
	bne.s   .failed
	cmpa.l  #0x98a8b8c8, a0
	bne.s   .failed
	cmpa.l  #0xa9b9c9d9, a1
	bne.s   .failed
	cmpa.l  #0xbacadaea, a2
	bne.s   .failed
	cmpa.l  #0xcbdbebfb, a3
	bne.s   .failed
	cmpa.l  #0xdcedfc0c, a4
	bne.s   .failed
	cmpa.l  #0xedfd0d1d, a5
	bne.s   .failed
	cmpa.l  #0xfe0e1e2e, a6
	bne.s   .failed
	bra.s   .restore

.failed:
	move.l #1, ss_register_failure

.restore:
	movem.l (sp)+, d2-d7/a2-a6
	rts
