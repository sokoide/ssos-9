.text
add.l  %d0, %d1
add.w  (%a0), %d2
add.b  (%a1)+, %d3
add.w  -(%a2), %d4
add.l  (8, %a3), %d5
adda.w %d0, %a0
adda.l (4, %a0), %a1
addi.b #1, %d0
addi.w #0x800, %d1
addi.l #0x12345, %d2
addq.l #1, %d3
addq.w #8, %a0
sub.l  %d0, %d1
sub.w  (2, %a0), %d2
suba.l %d0, %a0
subi.b #255, %d3
subi.w #-32768, %d4
subq.b #8, %d5
subq.l #3, %a1
cmp.l  %d0, %d1
cmp.w  (%a0), %d2
cmpa.l %d0, %a0
cmpi.b #7, %d3
cmpi.b #'a', %d0
cmpm.l (%a0)+, (%a1)+
and.w  %d0, %d1
andi.b #0xf, %d2
or.l   %d0, %d1
ori.w  #0x8000, %d2
eor.b  %d0, %d1
eori.l #0xffff, %d2
not.l  %d0
neg.w  %d1
negx.b %d2
muls.w %d0, %d1
mulu.w (4, %a0), %d2
divs.w %d0, %d1
divu.w %d2, %d3
ext.w  %d0
ext.l  %d1
swap   %d2
clr.l  %d0
clr.b  (%a0)
tst.w  %d1
tas    %d2
tas    (6, %a0)
chk.w  (2, %a0), %d3
abcd   %d0, %d1
sbcd   -(%a0), -(%a1)
nbcd   %d2
cmpi.w #(1<<2)+3, %d0
cmpi.w #1<<2+3, %d0
cmpi.l #(2+3)<<1, %d0
cmpi.l #1<<2*3, %d0
cmpi.l #(8/2)>>1, %d0
