| NOT BUILT — superseded by curve25519_sat.c (OPT #8b); kept as reference only.
|
| unused_donna_m68k.s  —  curve25519 field arithmetic, custom-written 68020 assembly
|
| fproduct_m68k: 10x10 polynomial multiply (replaces fproduct in donna.c)
|
| C prototype:
|   void fproduct_m68k(limb *output, const limb *in2, const limb *in);
|   limb = int64_t (8 bytes, big-endian: high word at lower address)
|   in2[i] / in[j] = (s32) of limb = low 32-bit word at offset i*8+4
|
| Key instruction:
|   MULS.L <ea>, Dh:Dl  — signed 32x32->64 bit
|   <ea> is read-only source; Dl is multiplicand + low-half destination;
|   Dh is high-half destination.  Clears X flag on exit.
|
| Register map:
|   d0    accumulator low  (current output[k])
|   d1    accumulator high
|   d2    in[j] load / MULS.L low result (temp)
|   d3    MULS.L high result (temp)
|   a2    output (auto-incremented through 19 x 8-byte slots)
|   a3    in2 base
|   a4    in base
|
| Input offsets from a3/a4 (i*8+4 for limb index i):
|   [0]=4  [1]=12  [2]=20  [3]=28  [4]=36
|   [5]=44 [6]=52  [7]=60  [8]=68  [9]=76
|
| Output written sequentially via (%a2)+ auto-increment (2x MOVE.L per slot).
|
| Stack frame: save d2-d3/a2-a4 (5 regs = 20 bytes)
|   24(%sp)=output  28(%sp)=in2  32(%sp)=in  (after save)

    .text
    .even
    .global fproduct_m68k

fproduct_m68k:
    movem.l %d2-%d3/%a2-%a4,-(%sp)
    movea.l 24(%sp),%a2         | output base
    movea.l 28(%sp),%a3         | in2 base
    movea.l 32(%sp),%a4         | in base

    | --- output[0] = in2[0]*in[0] ---
    move.l  4(%a4),%d0
    muls.l  4(%a3),%d1:%d0
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[1] = in2[0]*in[1] + in2[1]*in[0] ---
    move.l  12(%a4),%d0
    muls.l  4(%a3),%d1:%d0          | in2[0]*in[1]
    move.l  4(%a4),%d2
    muls.l  12(%a3),%d3:%d2         | in2[1]*in[0]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[2] = 2*(in2[1]*in[1]) + in2[0]*in[2] + in2[2]*in[0] ---
    move.l  12(%a4),%d0
    muls.l  12(%a3),%d1:%d0         | in2[1]*in[1]
    add.l   %d0,%d0                 | x2
    addx.l  %d1,%d1
    move.l  20(%a4),%d2
    muls.l  4(%a3),%d3:%d2          | in2[0]*in[2]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  4(%a4),%d2
    muls.l  20(%a3),%d3:%d2         | in2[2]*in[0]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[3] = in2[1]*in[2] + in2[2]*in[1] + in2[0]*in[3] + in2[3]*in[0] ---
    move.l  20(%a4),%d0
    muls.l  12(%a3),%d1:%d0         | in2[1]*in[2]
    move.l  12(%a4),%d2
    muls.l  20(%a3),%d3:%d2         | in2[2]*in[1]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  28(%a4),%d2
    muls.l  4(%a3),%d3:%d2          | in2[0]*in[3]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  4(%a4),%d2
    muls.l  28(%a3),%d3:%d2         | in2[3]*in[0]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[4] = in2[2]*in[2] + 2*(in2[1]*in[3]+in2[3]*in[1]) + in2[0]*in[4] + in2[4]*in[0] ---
    move.l  20(%a4),%d0
    muls.l  20(%a3),%d1:%d0         | in2[2]*in[2]
    move.l  28(%a4),%d2
    muls.l  12(%a3),%d3:%d2         | in2[1]*in[3]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  12(%a4),%d2
    muls.l  28(%a3),%d3:%d2         | in2[3]*in[1]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  36(%a4),%d2
    muls.l  4(%a3),%d3:%d2          | in2[0]*in[4]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  4(%a4),%d2
    muls.l  36(%a3),%d3:%d2         | in2[4]*in[0]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[5] = in2[2]*in[3]+in2[3]*in[2]+in2[1]*in[4]+in2[4]*in[1]+in2[0]*in[5]+in2[5]*in[0] ---
    move.l  28(%a4),%d0
    muls.l  20(%a3),%d1:%d0         | in2[2]*in[3]
    move.l  20(%a4),%d2
    muls.l  28(%a3),%d3:%d2         | in2[3]*in[2]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  36(%a4),%d2
    muls.l  12(%a3),%d3:%d2         | in2[1]*in[4]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  12(%a4),%d2
    muls.l  36(%a3),%d3:%d2         | in2[4]*in[1]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  44(%a4),%d2
    muls.l  4(%a3),%d3:%d2          | in2[0]*in[5]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  4(%a4),%d2
    muls.l  44(%a3),%d3:%d2         | in2[5]*in[0]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[6] = 2*(in2[3]*in[3]+in2[1]*in[5]+in2[5]*in[1])+in2[2]*in[4]+in2[4]*in[2]+in2[0]*in[6]+in2[6]*in[0] ---
    move.l  28(%a4),%d0
    muls.l  28(%a3),%d1:%d0         | in2[3]*in[3]
    add.l   %d0,%d0                 | x2
    addx.l  %d1,%d1
    move.l  44(%a4),%d2
    muls.l  12(%a3),%d3:%d2         | in2[1]*in[5]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  12(%a4),%d2
    muls.l  44(%a3),%d3:%d2         | in2[5]*in[1]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  36(%a4),%d2
    muls.l  20(%a3),%d3:%d2         | in2[2]*in[4]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  20(%a4),%d2
    muls.l  36(%a3),%d3:%d2         | in2[4]*in[2]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  52(%a4),%d2
    muls.l  4(%a3),%d3:%d2          | in2[0]*in[6]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  4(%a4),%d2
    muls.l  52(%a3),%d3:%d2         | in2[6]*in[0]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[7] = in2[3]*in[4]+in2[4]*in[3]+in2[2]*in[5]+in2[5]*in[2]+in2[1]*in[6]+in2[6]*in[1]+in2[0]*in[7]+in2[7]*in[0] ---
    move.l  36(%a4),%d0
    muls.l  28(%a3),%d1:%d0         | in2[3]*in[4]
    move.l  28(%a4),%d2
    muls.l  36(%a3),%d3:%d2         | in2[4]*in[3]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  44(%a4),%d2
    muls.l  20(%a3),%d3:%d2         | in2[2]*in[5]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  20(%a4),%d2
    muls.l  44(%a3),%d3:%d2         | in2[5]*in[2]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  52(%a4),%d2
    muls.l  12(%a3),%d3:%d2         | in2[1]*in[6]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  12(%a4),%d2
    muls.l  52(%a3),%d3:%d2         | in2[6]*in[1]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  60(%a4),%d2
    muls.l  4(%a3),%d3:%d2          | in2[0]*in[7]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  4(%a4),%d2
    muls.l  60(%a3),%d3:%d2         | in2[7]*in[0]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[8] = in2[4]*in[4]+2*(in2[3]*in[5]+in2[5]*in[3]+in2[1]*in[7]+in2[7]*in[1])+in2[2]*in[6]+in2[6]*in[2]+in2[0]*in[8]+in2[8]*in[0] ---
    move.l  36(%a4),%d0
    muls.l  36(%a3),%d1:%d0         | in2[4]*in[4]
    move.l  44(%a4),%d2
    muls.l  28(%a3),%d3:%d2         | in2[3]*in[5]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  28(%a4),%d2
    muls.l  44(%a3),%d3:%d2         | in2[5]*in[3]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  60(%a4),%d2
    muls.l  12(%a3),%d3:%d2         | in2[1]*in[7]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  12(%a4),%d2
    muls.l  60(%a3),%d3:%d2         | in2[7]*in[1]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  52(%a4),%d2
    muls.l  20(%a3),%d3:%d2         | in2[2]*in[6]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  20(%a4),%d2
    muls.l  52(%a3),%d3:%d2         | in2[6]*in[2]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  68(%a4),%d2
    muls.l  4(%a3),%d3:%d2          | in2[0]*in[8]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  4(%a4),%d2
    muls.l  68(%a3),%d3:%d2         | in2[8]*in[0]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[9] = in2[4]*in[5]+in2[5]*in[4]+in2[3]*in[6]+in2[6]*in[3]+in2[2]*in[7]+in2[7]*in[2]+in2[1]*in[8]+in2[8]*in[1]+in2[0]*in[9]+in2[9]*in[0] ---
    move.l  44(%a4),%d0
    muls.l  36(%a3),%d1:%d0         | in2[4]*in[5]
    move.l  36(%a4),%d2
    muls.l  44(%a3),%d3:%d2         | in2[5]*in[4]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  52(%a4),%d2
    muls.l  28(%a3),%d3:%d2         | in2[3]*in[6]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  28(%a4),%d2
    muls.l  52(%a3),%d3:%d2         | in2[6]*in[3]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  60(%a4),%d2
    muls.l  20(%a3),%d3:%d2         | in2[2]*in[7]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  20(%a4),%d2
    muls.l  60(%a3),%d3:%d2         | in2[7]*in[2]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  68(%a4),%d2
    muls.l  12(%a3),%d3:%d2         | in2[1]*in[8]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  12(%a4),%d2
    muls.l  68(%a3),%d3:%d2         | in2[8]*in[1]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  76(%a4),%d2
    muls.l  4(%a3),%d3:%d2          | in2[0]*in[9]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  4(%a4),%d2
    muls.l  76(%a3),%d3:%d2         | in2[9]*in[0]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[10] = 2*(in2[5]*in[5]+in2[3]*in[7]+in2[7]*in[3]+in2[1]*in[9]+in2[9]*in[1])+in2[4]*in[6]+in2[6]*in[4]+in2[2]*in[8]+in2[8]*in[2] ---
    move.l  44(%a4),%d0
    muls.l  44(%a3),%d1:%d0         | in2[5]*in[5]
    add.l   %d0,%d0                 | x2
    addx.l  %d1,%d1
    move.l  60(%a4),%d2
    muls.l  28(%a3),%d3:%d2         | in2[3]*in[7]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  28(%a4),%d2
    muls.l  60(%a3),%d3:%d2         | in2[7]*in[3]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  76(%a4),%d2
    muls.l  12(%a3),%d3:%d2         | in2[1]*in[9]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  12(%a4),%d2
    muls.l  76(%a3),%d3:%d2         | in2[9]*in[1]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  52(%a4),%d2
    muls.l  36(%a3),%d3:%d2         | in2[4]*in[6]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  36(%a4),%d2
    muls.l  52(%a3),%d3:%d2         | in2[6]*in[4]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  68(%a4),%d2
    muls.l  20(%a3),%d3:%d2         | in2[2]*in[8]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  20(%a4),%d2
    muls.l  68(%a3),%d3:%d2         | in2[8]*in[2]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[11] = in2[5]*in[6]+in2[6]*in[5]+in2[4]*in[7]+in2[7]*in[4]+in2[3]*in[8]+in2[8]*in[3]+in2[2]*in[9]+in2[9]*in[2] ---
    move.l  52(%a4),%d0
    muls.l  44(%a3),%d1:%d0         | in2[5]*in[6]
    move.l  44(%a4),%d2
    muls.l  52(%a3),%d3:%d2         | in2[6]*in[5]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  60(%a4),%d2
    muls.l  36(%a3),%d3:%d2         | in2[4]*in[7]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  36(%a4),%d2
    muls.l  60(%a3),%d3:%d2         | in2[7]*in[4]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  68(%a4),%d2
    muls.l  28(%a3),%d3:%d2         | in2[3]*in[8]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  28(%a4),%d2
    muls.l  68(%a3),%d3:%d2         | in2[8]*in[3]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  76(%a4),%d2
    muls.l  20(%a3),%d3:%d2         | in2[2]*in[9]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  20(%a4),%d2
    muls.l  76(%a3),%d3:%d2         | in2[9]*in[2]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[12] = in2[6]*in[6]+2*(in2[5]*in[7]+in2[7]*in[5]+in2[3]*in[9]+in2[9]*in[3])+in2[4]*in[8]+in2[8]*in[4] ---
    move.l  52(%a4),%d0
    muls.l  52(%a3),%d1:%d0         | in2[6]*in[6]
    move.l  60(%a4),%d2
    muls.l  44(%a3),%d3:%d2         | in2[5]*in[7]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  44(%a4),%d2
    muls.l  60(%a3),%d3:%d2         | in2[7]*in[5]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  76(%a4),%d2
    muls.l  28(%a3),%d3:%d2         | in2[3]*in[9]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  28(%a4),%d2
    muls.l  76(%a3),%d3:%d2         | in2[9]*in[3]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  68(%a4),%d2
    muls.l  36(%a3),%d3:%d2         | in2[4]*in[8]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  36(%a4),%d2
    muls.l  68(%a3),%d3:%d2         | in2[8]*in[4]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[13] = in2[6]*in[7]+in2[7]*in[6]+in2[5]*in[8]+in2[8]*in[5]+in2[4]*in[9]+in2[9]*in[4] ---
    move.l  60(%a4),%d0
    muls.l  52(%a3),%d1:%d0         | in2[6]*in[7]
    move.l  52(%a4),%d2
    muls.l  60(%a3),%d3:%d2         | in2[7]*in[6]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  68(%a4),%d2
    muls.l  44(%a3),%d3:%d2         | in2[5]*in[8]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  44(%a4),%d2
    muls.l  68(%a3),%d3:%d2         | in2[8]*in[5]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  76(%a4),%d2
    muls.l  36(%a3),%d3:%d2         | in2[4]*in[9]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  36(%a4),%d2
    muls.l  76(%a3),%d3:%d2         | in2[9]*in[4]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[14] = 2*(in2[7]*in[7]+in2[5]*in[9]+in2[9]*in[5])+in2[6]*in[8]+in2[8]*in[6] ---
    move.l  60(%a4),%d0
    muls.l  60(%a3),%d1:%d0         | in2[7]*in[7]
    add.l   %d0,%d0                 | x2
    addx.l  %d1,%d1
    move.l  76(%a4),%d2
    muls.l  44(%a3),%d3:%d2         | in2[5]*in[9]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  44(%a4),%d2
    muls.l  76(%a3),%d3:%d2         | in2[9]*in[5]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  68(%a4),%d2
    muls.l  52(%a3),%d3:%d2         | in2[6]*in[8]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  52(%a4),%d2
    muls.l  68(%a3),%d3:%d2         | in2[8]*in[6]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[15] = in2[7]*in[8]+in2[8]*in[7]+in2[6]*in[9]+in2[9]*in[6] ---
    move.l  68(%a4),%d0
    muls.l  60(%a3),%d1:%d0         | in2[7]*in[8]
    move.l  60(%a4),%d2
    muls.l  68(%a3),%d3:%d2         | in2[8]*in[7]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  76(%a4),%d2
    muls.l  52(%a3),%d3:%d2         | in2[6]*in[9]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  52(%a4),%d2
    muls.l  76(%a3),%d3:%d2         | in2[9]*in[6]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[16] = in2[8]*in[8]+2*(in2[7]*in[9]+in2[9]*in[7]) ---
    move.l  68(%a4),%d0
    muls.l  68(%a3),%d1:%d0         | in2[8]*in[8]
    move.l  76(%a4),%d2
    muls.l  60(%a3),%d3:%d2         | in2[7]*in[9]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  60(%a4),%d2
    muls.l  76(%a3),%d3:%d2         | in2[9]*in[7]
    add.l   %d2,%d2                 | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[17] = in2[8]*in[9]+in2[9]*in[8] ---
    move.l  76(%a4),%d0
    muls.l  68(%a3),%d1:%d0         | in2[8]*in[9]
    move.l  68(%a4),%d2
    muls.l  76(%a3),%d3:%d2         | in2[9]*in[8]
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[18] = 2*(in2[9]*in[9]) ---
    move.l  76(%a4),%d0
    muls.l  76(%a3),%d1:%d0         | in2[9]*in[9]
    add.l   %d0,%d0                 | x2
    addx.l  %d1,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    movem.l (%sp)+,%d2-%d3/%a2-%a4
    rts

| -----------------------------------------------------------------------
| fsquare_inner_m68k: 10-element squaring (replaces fsquare_inner)
|
| C prototype:
|   void fsquare_inner_m68k(limb *output, const limb *in);
|   Both operands come from the same array (in); 55 products total.
|
| Register map:
|   d0    accumulator low
|   d1    accumulator high
|   d2    temp Dl (MULS.L low result)
|   d3    temp Dh (MULS.L high result)
|   a2    output (auto-incremented through 19 x 8-byte slots)
|   a3    in base
|
| Input offsets (i*8+4, big-endian low word of int64_t):
|   [0]=4  [1]=12  [2]=20  [3]=28  [4]=36
|   [5]=44 [6]=52  [7]=60  [8]=68  [9]=76
|
| Stack frame: save d2-d3/a2-a3 (4 regs = 16 bytes)
|   20(%sp)=output  24(%sp)=in  (after save)
|
| Coefficient patterns used:
|   FIRST(i,j)  : d1:d0  = in[i]*in[j]         (starts accumulator)
|   FIRST2(i,j) : d1:d0  = 2*in[i]*in[j]       (starts, then doubles)
|   ACCUM(i,j)  : d1:d0 += in[i]*in[j]
|   ACCUM2(i,j) : d1:d0 += 2*in[i]*in[j]
|   ACCUM4(i,j) : d1:d0 += 4*in[i]*in[j]

    .text
    .even
    .global fsquare_inner_m68k

fsquare_inner_m68k:
    movem.l %d2-%d3/%a2-%a3,-(%sp)
    movea.l 20(%sp),%a2         | output base
    movea.l 24(%sp),%a3         | in base

    | --- output[0] = in[0]^2 ---
    move.l  4(%a3),%d0
    muls.l  4(%a3),%d1:%d0
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[1] = 2*in[0]*in[1] ---
    move.l  12(%a3),%d0
    muls.l  4(%a3),%d1:%d0
    add.l   %d0,%d0
    addx.l  %d1,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[2] = 2*in[1]^2 + 2*in[0]*in[2] ---
    move.l  12(%a3),%d0
    muls.l  12(%a3),%d1:%d0          | in[1]^2
    add.l   %d0,%d0                   | x2
    addx.l  %d1,%d1
    move.l  20(%a3),%d2
    muls.l  4(%a3),%d3:%d2            | in[0]*in[2]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[3] = 2*in[1]*in[2] + 2*in[0]*in[3] ---
    move.l  20(%a3),%d0
    muls.l  12(%a3),%d1:%d0           | in[1]*in[2]
    add.l   %d0,%d0                   | x2
    addx.l  %d1,%d1
    move.l  28(%a3),%d2
    muls.l  4(%a3),%d3:%d2            | in[0]*in[3]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[4] = in[2]^2 + 4*in[1]*in[3] + 2*in[0]*in[4] ---
    move.l  20(%a3),%d0
    muls.l  20(%a3),%d1:%d0           | in[2]^2
    move.l  28(%a3),%d2
    muls.l  12(%a3),%d3:%d2           | in[1]*in[3]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d2                   | x4
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  36(%a3),%d2
    muls.l  4(%a3),%d3:%d2            | in[0]*in[4]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[5] = 2*in[2]*in[3] + 2*in[1]*in[4] + 2*in[0]*in[5] ---
    move.l  28(%a3),%d0
    muls.l  20(%a3),%d1:%d0           | in[2]*in[3]
    add.l   %d0,%d0                   | x2
    addx.l  %d1,%d1
    move.l  36(%a3),%d2
    muls.l  12(%a3),%d3:%d2           | in[1]*in[4]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  44(%a3),%d2
    muls.l  4(%a3),%d3:%d2            | in[0]*in[5]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[6] = 2*in[3]^2 + 2*in[2]*in[4] + 2*in[0]*in[6] + 4*in[1]*in[5] ---
    move.l  28(%a3),%d0
    muls.l  28(%a3),%d1:%d0           | in[3]^2
    add.l   %d0,%d0                   | x2
    addx.l  %d1,%d1
    move.l  36(%a3),%d2
    muls.l  20(%a3),%d3:%d2           | in[2]*in[4]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  52(%a3),%d2
    muls.l  4(%a3),%d3:%d2            | in[0]*in[6]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  44(%a3),%d2
    muls.l  12(%a3),%d3:%d2           | in[1]*in[5]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d2                   | x4
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[7] = 2*in[3]*in[4] + 2*in[2]*in[5] + 2*in[1]*in[6] + 2*in[0]*in[7] ---
    move.l  36(%a3),%d0
    muls.l  28(%a3),%d1:%d0           | in[3]*in[4]
    add.l   %d0,%d0                   | x2
    addx.l  %d1,%d1
    move.l  44(%a3),%d2
    muls.l  20(%a3),%d3:%d2           | in[2]*in[5]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  52(%a3),%d2
    muls.l  12(%a3),%d3:%d2           | in[1]*in[6]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  60(%a3),%d2
    muls.l  4(%a3),%d3:%d2            | in[0]*in[7]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[8] = in[4]^2 + 2*in[2]*in[6] + 2*in[0]*in[8] + 4*in[1]*in[7] + 4*in[3]*in[5] ---
    move.l  36(%a3),%d0
    muls.l  36(%a3),%d1:%d0           | in[4]^2
    move.l  52(%a3),%d2
    muls.l  20(%a3),%d3:%d2           | in[2]*in[6]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  68(%a3),%d2
    muls.l  4(%a3),%d3:%d2            | in[0]*in[8]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  60(%a3),%d2
    muls.l  12(%a3),%d3:%d2           | in[1]*in[7]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d2                   | x4
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  44(%a3),%d2
    muls.l  28(%a3),%d3:%d2           | in[3]*in[5]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d2                   | x4
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[9] = 2*in[4]*in[5] + 2*in[3]*in[6] + 2*in[2]*in[7] + 2*in[1]*in[8] + 2*in[0]*in[9] ---
    move.l  44(%a3),%d0
    muls.l  36(%a3),%d1:%d0           | in[4]*in[5]
    add.l   %d0,%d0                   | x2
    addx.l  %d1,%d1
    move.l  52(%a3),%d2
    muls.l  28(%a3),%d3:%d2           | in[3]*in[6]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  60(%a3),%d2
    muls.l  20(%a3),%d3:%d2           | in[2]*in[7]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  68(%a3),%d2
    muls.l  12(%a3),%d3:%d2           | in[1]*in[8]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  76(%a3),%d2
    muls.l  4(%a3),%d3:%d2            | in[0]*in[9]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[10] = 2*in[5]^2 + 2*in[4]*in[6] + 2*in[2]*in[8] + 4*in[3]*in[7] + 4*in[1]*in[9] ---
    move.l  44(%a3),%d0
    muls.l  44(%a3),%d1:%d0           | in[5]^2
    add.l   %d0,%d0                   | x2
    addx.l  %d1,%d1
    move.l  52(%a3),%d2
    muls.l  36(%a3),%d3:%d2           | in[4]*in[6]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  68(%a3),%d2
    muls.l  20(%a3),%d3:%d2           | in[2]*in[8]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  60(%a3),%d2
    muls.l  28(%a3),%d3:%d2           | in[3]*in[7]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d2                   | x4
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  76(%a3),%d2
    muls.l  12(%a3),%d3:%d2           | in[1]*in[9]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d2                   | x4
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[11] = 2*in[5]*in[6] + 2*in[4]*in[7] + 2*in[3]*in[8] + 2*in[2]*in[9] ---
    move.l  52(%a3),%d0
    muls.l  44(%a3),%d1:%d0           | in[5]*in[6]
    add.l   %d0,%d0                   | x2
    addx.l  %d1,%d1
    move.l  60(%a3),%d2
    muls.l  36(%a3),%d3:%d2           | in[4]*in[7]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  68(%a3),%d2
    muls.l  28(%a3),%d3:%d2           | in[3]*in[8]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  76(%a3),%d2
    muls.l  20(%a3),%d3:%d2           | in[2]*in[9]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[12] = in[6]^2 + 2*in[4]*in[8] + 4*in[5]*in[7] + 4*in[3]*in[9] ---
    move.l  52(%a3),%d0
    muls.l  52(%a3),%d1:%d0           | in[6]^2
    move.l  68(%a3),%d2
    muls.l  36(%a3),%d3:%d2           | in[4]*in[8]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  60(%a3),%d2
    muls.l  44(%a3),%d3:%d2           | in[5]*in[7]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d2                   | x4
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  76(%a3),%d2
    muls.l  28(%a3),%d3:%d2           | in[3]*in[9]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d2                   | x4
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[13] = 2*in[6]*in[7] + 2*in[5]*in[8] + 2*in[4]*in[9] ---
    move.l  60(%a3),%d0
    muls.l  52(%a3),%d1:%d0           | in[6]*in[7]
    add.l   %d0,%d0                   | x2
    addx.l  %d1,%d1
    move.l  68(%a3),%d2
    muls.l  44(%a3),%d3:%d2           | in[5]*in[8]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  76(%a3),%d2
    muls.l  36(%a3),%d3:%d2           | in[4]*in[9]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[14] = 2*in[7]^2 + 2*in[6]*in[8] + 4*in[5]*in[9] ---
    move.l  60(%a3),%d0
    muls.l  60(%a3),%d1:%d0           | in[7]^2
    add.l   %d0,%d0                   | x2
    addx.l  %d1,%d1
    move.l  68(%a3),%d2
    muls.l  52(%a3),%d3:%d2           | in[6]*in[8]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  76(%a3),%d2
    muls.l  44(%a3),%d3:%d2           | in[5]*in[9]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d2                   | x4
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[15] = 2*in[7]*in[8] + 2*in[6]*in[9] ---
    move.l  68(%a3),%d0
    muls.l  60(%a3),%d1:%d0           | in[7]*in[8]
    add.l   %d0,%d0                   | x2
    addx.l  %d1,%d1
    move.l  76(%a3),%d2
    muls.l  52(%a3),%d3:%d2           | in[6]*in[9]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[16] = in[8]^2 + 4*in[7]*in[9] ---
    move.l  68(%a3),%d0
    muls.l  68(%a3),%d1:%d0           | in[8]^2
    move.l  76(%a3),%d2
    muls.l  60(%a3),%d3:%d2           | in[7]*in[9]
    add.l   %d2,%d2                   | x2
    addx.l  %d3,%d3
    add.l   %d2,%d2                   | x4
    addx.l  %d3,%d3
    add.l   %d2,%d0
    addx.l  %d3,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[17] = 2*in[8]*in[9] ---
    move.l  76(%a3),%d0
    muls.l  68(%a3),%d1:%d0           | in[8]*in[9]
    add.l   %d0,%d0                   | x2
    addx.l  %d1,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    | --- output[18] = 2*in[9]^2 ---
    move.l  76(%a3),%d0
    muls.l  76(%a3),%d1:%d0           | in[9]^2
    add.l   %d0,%d0                   | x2
    addx.l  %d1,%d1
    move.l  %d1,(%a2)+
    move.l  %d0,(%a2)+

    movem.l (%sp)+,%d2-%d3/%a2-%a3
    rts
