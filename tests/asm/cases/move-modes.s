.text
move.l  %d0, %d1
move.w  %a2, %d3
move.b  (%a0), %d4
move.l  (32, %a1), %d5
move.w  (4, %a0, %d1.l), %d6
move.w  (8, %a0, %d2.w), %d7
move.l  (%a2)+, %d0
move.w  -(%a3), %d1
move.l  0x20000, %d2
move.w  %d0, 0x20004
move.l  (16, %pc), %d3
move.w  (4, %pc, %d0.l), %d4
move.l  #0x12345678, %d5
move.w  #1024, %d6
move.b  #'A,%d7
movea.l 0x20008, %a0
movea.w %a0, %a1
move.l  %d0, (%a4)
move.w  %d1, (12, %a5)
move.b  %d2, (%a6)+
move.l  %d3, -(%a7)
move.l  %d4, (2, %a0, %d3.l)
move.w  %d5, 0x20010
move.b  %d6, 0x20011
