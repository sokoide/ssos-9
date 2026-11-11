.text
asl.l  #1, %d0
asl.b  #8, %d1
asl.w  #3, %d2
asr.l  %d3, %d4
asr.w  #8, %d5
lsl.b  #2, %d6
lsr.l  #1, %d7
lsl.w  %d0, %d1
lsr.b  %d2, %d3
rol.l  #4, %d0
ror.w  #1, %d1
roxl.b #8, %d2
roxr.l #2, %d3
rol.w  %d4, %d5
asl    (4, %a0)
asr    (%a1)+
lsl    -(%a2)
lsr    (2, %a3)
rol    (6, %a4)
ror    (%a5)+
roxl   -(%a6)
roxr   0x20000
bset   #2, %d0
bset   #7, (4, %a0)
bclr   %d1, %d2
bclr   #1, (%a1)+
bchg   #3, %d3
bchg   %d4, (2, %a2)
btst   #0, %d5
btst   %d6, -(%a3)
btst   #4, 0x20020
