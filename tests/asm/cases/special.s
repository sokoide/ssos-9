.text
lea     (8, %a0), %a1
lea     (16, %pc), %a2
lea     0x20000, %a3
pea     (12, %a0)
pea     0x20004
exg     %d0, %d1
exg     %a0, %a1
exg     %d2, %a2
link    %a6, #-8
unlk    %a6
moveq   #0, %d0
moveq   #127, %d1
moveq   #-128, %d2
move.w  %d0, %ccr
move.w  %sr, %d2
move.w  %d3, %sr
move.l  %a0, %usp
move.l  %usp, %a1
movem.l %d0-%d3/%a0, (%a7)
movem.l (%a7), %d0-%d3/%a0
movem.w %d0-%d7/%a1-%a3, -(%a7)
movem.w (%a7)+, %d0-%d7/%a1-%a3
movep.l %d0, 16(%a0)
movep.w 8(%a1), %d1
movep.w %d2, 4(%a2)
nop
rtr
rte
illegal
trap    #3
trap    #15
trapv
stop    #0x2700
reset
