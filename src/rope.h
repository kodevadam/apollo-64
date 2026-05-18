#ifndef APOLLO64_ROPE_H
#define APOLLO64_ROPE_H

/*
 * Core rope memory: the AGC's ROM. Layout matches the .bin output of
 * yaYUL (the AGC assembler): one entry per 16-bit word, big-endian,
 * 15 bits of data + 1 bit of parity (LSB).
 *
 * 044 (octal) = 36 fixed banks; 02000 = 1024 words/bank.
 * Bank order in the file is 2,3,0,1,4..35 - reordering is done at
 * load time (see agc_host.c::agc_host_init).
 *
 * Default build ships a zeroed rope so the project links cleanly. To run a
 * real mission, regenerate src/rope.c with:
 *
 *   tools/bin2rope Luminary099.bin > src/rope.c
 *
 * See BUILDING.md for the full toolchain instructions.
 */

#define CORE_SIZE (044 * 02000)

extern const unsigned char CoreRope[CORE_SIZE][2];

#endif
