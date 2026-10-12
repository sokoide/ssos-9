.section .text
.align   2
.globl   ss_test_install_syscall_vector
.globl   ss_test_install_ctrl_c_vector
.globl   ss_test_syscall_roundtrip
.extern  ss_syscall_entry
.extern  ss_ctrl_c_trap_handler

ss_test_install_syscall_vector:
	lea    ss_syscall_entry, a0
	move.l a0, 0x80
	rts

ss_test_install_ctrl_c_vector:
	lea    ss_ctrl_c_trap_handler, a0
	move.l a0, 0xb4
	rts

ss_test_syscall_roundtrip:
	movem.l d2-d7/a2-a6, -(sp)
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
	moveq   #0, d0
	trap    #0
	cmpi.l  #1, d0
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
	moveq   #0, d0
	bra.s   .restore

.failed:
	moveq #1, d0

.restore:
	movem.l (sp)+, d2-d7/a2-a6
	rts
