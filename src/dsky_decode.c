/*
 * Pure-C DSKY decode. No libdragon dependency.
 *
 * The AGC writes the display via channel 010: each write encodes a "relay
 * row" select (bits 14-11) and two 5-bit digit codes (bits 10-6 and 5-1).
 * Some rows also carry a sign bit (0x0400). The engine pre-latches the
 * 11-bit payload into State->OutputChannel10[row]; we mirror that into
 * g_dsky.latch[] in agc_host.c. This file turns that latch array into a
 * decoded panel ready for rendering.
 *
 * Relay row -> register mapping (cross-checked against yaDSKY2.cpp):
 *   11 -> MD1 MD2     (PROG)
 *   10 -> VD1 VD2     (VERB)
 *    9 -> ND1 ND2     (NOUN)
 *    8 -> -   R1D1
 *    7 -> R1D2 R1D3   + R1 plus-sign bit
 *    6 -> R1D4 R1D5   + R1 minus-sign bit
 *    5 -> R2D1 R2D2   + R2 plus-sign bit
 *    4 -> R2D3 R2D4   + R2 minus-sign bit
 *    3 -> R2D5 R3D1
 *    2 -> R3D2 R3D3   + R3 plus-sign bit
 *    1 -> R3D4 R3D5   + R3 minus-sign bit
 */

#include <string.h>

#include "dsky_decode.h"
#include "../vendor/yaAGC/agc_engine.h"

char
dsky_decode_digit(uint8_t code)
{
  /* The 5-bit relay code -> digit table, straight from
   * PINBALL_GAME__BUTTONS_AND_LIGHTS.agc:446 ("THE 5 BIT OUTPUT RELAY
   * CODES ARE"). yaDSKY2's 7Seg-NN.jpg filenames use NN in *decimal*;
   * these case labels are octal, so e.g. 7Seg-21 (decimal 21) is octal
   * 025 = '0'. Getting the radix wrong here only mis-decodes 0/2/3/4 -
   * the all-8s lamp test still looks right - so it is an easy bug to
   * miss. */
  switch (code & 0x1F) {
    case 025: return '0';   /* 10101 */
    case 003: return '1';   /* 00011 */
    case 031: return '2';   /* 11001 */
    case 033: return '3';   /* 11011 */
    case 017: return '4';   /* 01111 */
    case 036: return '5';   /* 11110 */
    case 034: return '6';   /* 11100 */
    case 023: return '7';   /* 10011 */
    case 035: return '8';   /* 11101 */
    case 037: return '9';   /* 11111 */
    default:  return ' ';
  }
}

static char
decode_sign(uint8_t bits)
{
  /* bits: bit1 = plus, bit0 = minus. Plus wins ties (matches yaDSKY2). */
  if (bits & 2) return '+';
  if (bits & 1) return '-';
  return ' ';
}

void
dsky_decode_panel(const volatile dsky_snapshot_t *src, dsky_panel_t *dst)
{
  memset(dst, 0, sizeof *dst);

  uint16_t r11 = src->latch[11], r10 = src->latch[10], r9 = src->latch[9];
  uint16_t r8  = src->latch[8],  r7  = src->latch[7],  r6 = src->latch[6];
  uint16_t r5  = src->latch[5],  r4  = src->latch[4],  r3 = src->latch[3];
  uint16_t r2  = src->latch[2],  r1  = src->latch[1];

  dst->prog[0] = dsky_decode_digit((r11 >> 5) & 0x1F);
  dst->prog[1] = dsky_decode_digit(r11 & 0x1F);
  dst->verb[0] = dsky_decode_digit((r10 >> 5) & 0x1F);
  dst->verb[1] = dsky_decode_digit(r10 & 0x1F);
  dst->noun[0] = dsky_decode_digit((r9  >> 5) & 0x1F);
  dst->noun[1] = dsky_decode_digit(r9  & 0x1F);

  uint8_t r1sign = ((r7 & 0x0400) ? 2 : 0) | ((r6 & 0x0400) ? 1 : 0);
  dst->s1    = decode_sign(r1sign);
  dst->r1[0] = dsky_decode_digit(r8 & 0x1F);
  dst->r1[1] = dsky_decode_digit((r7 >> 5) & 0x1F);
  dst->r1[2] = dsky_decode_digit(r7 & 0x1F);
  dst->r1[3] = dsky_decode_digit((r6 >> 5) & 0x1F);
  dst->r1[4] = dsky_decode_digit(r6 & 0x1F);

  uint8_t r2sign = ((r5 & 0x0400) ? 2 : 0) | ((r4 & 0x0400) ? 1 : 0);
  dst->s2    = decode_sign(r2sign);
  dst->r2[0] = dsky_decode_digit((r5 >> 5) & 0x1F);
  dst->r2[1] = dsky_decode_digit(r5 & 0x1F);
  dst->r2[2] = dsky_decode_digit((r4 >> 5) & 0x1F);
  dst->r2[3] = dsky_decode_digit(r4 & 0x1F);
  dst->r2[4] = dsky_decode_digit((r3 >> 5) & 0x1F);

  uint8_t r3sign = ((r2 & 0x0400) ? 2 : 0) | ((r1 & 0x0400) ? 1 : 0);
  dst->s3    = decode_sign(r3sign);
  dst->r3[0] = dsky_decode_digit(r3 & 0x1F);
  dst->r3[1] = dsky_decode_digit((r2 >> 5) & 0x1F);
  dst->r3[2] = dsky_decode_digit(r2 & 0x1F);
  dst->r3[3] = dsky_decode_digit((r1 >> 5) & 0x1F);
  dst->r3[4] = dsky_decode_digit(r1 & 0x1F);

  uint16_t c11  = src->channel11;
  uint16_t c163 = src->channel163;

  dst->comp_acty   = (c11 & 002)   != 0;
  dst->uplink_acty = (c11 & 004)   != 0;
  dst->no_att      = (c11 & 010)   != 0;
  dst->tracker     = (c11 & 0200)  != 0;
  dst->alt         = (c11 & 0400)  != 0;
  dst->vel         = (c11 & 01000) != 0;

  dst->standby     = (c163 & DSKY_STBY)     != 0;
  dst->key_rel     = (c163 & DSKY_KEY_REL)  != 0;
  dst->opr_err     = (c163 & DSKY_OPER_ERR) != 0;
  dst->prog_alarm  = (c163 & DSKY_VN_FLASH) != 0;
  dst->restart     = (c163 & DSKY_RESTART)  != 0;
  dst->temp        = (c163 & DSKY_TEMP)     != 0;
}
