.text

start:
	bra.w ok

skip1:
	bra.s start
	bsr.w sub1
	beq.w start

skip2:
	bra.s skip1
	bcs.s start
	bge.w ok
	bgt.s skip2

loop:
	ble.w loop
	blt.s loop
	bhi.w done
	bls.s done
	bmi.w done
	bpl.w done
	bvc.w done
	bvs.w done
	bra.w 1f

1:
	bra.s 1b
	bra.w 2f

2:
	bra.s 2b
	dbra  %d0, loop
	dbeq  %d1, loop
	dbne  %d2, 2b
	seq   %d0
	sne   (4, %a0)
	scc   %d1
	scs   -(%a1)
	spl   %d2
	smi   %d3
	sge   %d4
	slt   %d5
	sgt   %d6
	sle   %d7
	shi   %d0
	sls   %d1
	svs   %d2
	svc   %d3

ok:
	jmp (4, %a0)
	jsr (8, %a1)
	jmp (4, %pc, %d0.l)
jsr (12, %pc)

done:
	rts

err:
	rts

sub1:
	rts
