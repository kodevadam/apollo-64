/*
 * apollo-64: DSKY renderer.
 *
 * The DSKY (Display & Keyboard) on the real Apollo 11 was a panel with:
 *   - 3 two-digit numeric displays: PROG, VERB, NOUN
 *   - 3 five-digit signed numeric displays: R1, R2, R3
 *   - ~14 status lamps (COMP ACTY, UPLINK ACTY, KEY REL, OPR ERR, etc.)
 *   - A 19-key keypad
 *
 * The AGC drives the panel through channel 010: each write picks one of
 * 16 "relay rows" via bits 14-11 and pushes two 5-bit digit codes (bits
 * 10-6 and 5-1) into that row. The engine has already done the
 * latch-by-row bookkeeping for us (State->OutputChannel10[16]); we just
 * decode and draw.
 *
 * Decoding table cross-references yaDSKY2.cpp (Virtual AGC project).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <libdragon.h>

#include "dsky.h"
#include "agc_host.h"

/* 5-bit AGC display code -> ASCII digit. 0 is blank; everything not in
 * this table is invalid (treated as blank). */
static char
decode_digit(uint8_t code)
{
  switch (code & 0x1F) {
    case 021: return '0';
    case 003: return '1';
    case 025: return '2';
    case 027: return '3';
    case 015: return '4';
    case 036: return '5';
    case 034: return '6';
    case 023: return '7';
    case 035: return '8';
    case 037: return '9';
    default:  return ' ';
  }
}

/* Decoded panel state. Rebuilt from g_dsky every frame. */
typedef struct {
  char prog[2];     /* PROG */
  char verb[2];     /* VERB */
  char noun[2];     /* NOUN */
  char r1[5];       /* R1 digits (no sign char) */
  char r2[5];
  char r3[5];
  char s1;          /* '+', '-', or ' ' */
  char s2;
  char s3;
  bool comp_acty;
  bool uplink_acty;
  bool no_att;
  bool standby;
  bool key_rel;
  bool opr_err;
  bool prio_disp;
  bool temp;
  bool gimbal_lock;
  bool prog_alarm;
  bool restart;
  bool tracker;
  bool alt;
  bool vel;
} panel_t;

static uint32_t last_generation;
static panel_t  panel;

/* Decode (R_plus<<1) | R_minus into a single sign char. + wins ties. */
static char
decode_sign(uint8_t bits)
{
  if (bits & 2) return '+';
  if (bits & 1) return '-';
  return ' ';
}

/* Rebuild `panel` from g_dsky. Cheap; safe to call every frame. */
static void
decode_panel(void)
{
  const volatile dsky_snapshot_t *d = &g_dsky;

  /* OutputChannel10[i] for relay row i holds the low 11 bits the AGC
   * wrote (top 4 bits are the row select, stripped by the engine).
   * Bit 0x0400 is the sign flag for sign-bearing rows. */
  memset(&panel, 0, sizeof panel);

  uint16_t r11 = d->latch[11], r10 = d->latch[10], r9 = d->latch[9];
  uint16_t r8 = d->latch[8],   r7  = d->latch[7],  r6 = d->latch[6];
  uint16_t r5 = d->latch[5],   r4  = d->latch[4],  r3 = d->latch[3];
  uint16_t r2 = d->latch[2],   r1  = d->latch[1];

  panel.prog[0] = decode_digit((r11 >> 5) & 0x1F);
  panel.prog[1] = decode_digit(r11 & 0x1F);
  panel.verb[0] = decode_digit((r10 >> 5) & 0x1F);
  panel.verb[1] = decode_digit(r10 & 0x1F);
  panel.noun[0] = decode_digit((r9  >> 5) & 0x1F);
  panel.noun[1] = decode_digit(r9  & 0x1F);

  /* R1: row 8 = D1 (right only); row 7 = sign+ + D2 + D3; row 6 = sign- + D4 + D5 */
  uint8_t r1sign = ((r7 & 0x0400) ? 2 : 0) | ((r6 & 0x0400) ? 1 : 0);
  panel.s1    = decode_sign(r1sign);
  panel.r1[0] = decode_digit(r8 & 0x1F);
  panel.r1[1] = decode_digit((r7 >> 5) & 0x1F);
  panel.r1[2] = decode_digit(r7 & 0x1F);
  panel.r1[3] = decode_digit((r6 >> 5) & 0x1F);
  panel.r1[4] = decode_digit(r6 & 0x1F);

  /* R2: row 5 = sign+ + D1 + D2; row 4 = sign- + D3 + D4; row 3 = D5 (left) + R3D1 (right) */
  uint8_t r2sign = ((r5 & 0x0400) ? 2 : 0) | ((r4 & 0x0400) ? 1 : 0);
  panel.s2    = decode_sign(r2sign);
  panel.r2[0] = decode_digit((r5 >> 5) & 0x1F);
  panel.r2[1] = decode_digit(r5 & 0x1F);
  panel.r2[2] = decode_digit((r4 >> 5) & 0x1F);
  panel.r2[3] = decode_digit(r4 & 0x1F);
  panel.r2[4] = decode_digit((r3 >> 5) & 0x1F);

  /* R3: row 3 (right) = D1; row 2 = sign+ + D2 + D3; row 1 = sign- + D4 + D5 */
  uint8_t r3sign = ((r2 & 0x0400) ? 2 : 0) | ((r1 & 0x0400) ? 1 : 0);
  panel.s3    = decode_sign(r3sign);
  panel.r3[0] = decode_digit(r3 & 0x1F);
  panel.r3[1] = decode_digit((r2 >> 5) & 0x1F);
  panel.r3[2] = decode_digit(r2 & 0x1F);
  panel.r3[3] = decode_digit((r1 >> 5) & 0x1F);
  panel.r3[4] = decode_digit(r1 & 0x1F);

  /* Lamps. Channel 011 carries the common status lamps; channel 0163 is
   * the synthesised "DSKY-as-channel" pseudo-channel the engine maintains
   * (DSKY_AGC_WARN, DSKY_TEMP, ...). */
  uint16_t c11  = d->channel11;
  uint16_t c163 = d->channel163;

  panel.comp_acty   = (c11 & 002) != 0;
  panel.uplink_acty = (c11 & 004) != 0;
  panel.no_att      = (c11 & 010) != 0;
  panel.tracker     = (c11 & 0200) != 0;
  panel.alt         = (c11 & 0400) != 0;
  panel.vel         = (c11 & 01000) != 0;

  panel.standby     = (c163 & DSKY_STBY)     != 0;
  panel.key_rel     = (c163 & DSKY_KEY_REL)  != 0;
  panel.opr_err     = (c163 & DSKY_OPER_ERR) != 0;
  panel.prog_alarm  = (c163 & DSKY_VN_FLASH) != 0;  /* shares VN flash bit on synth chan */
  panel.restart     = (c163 & DSKY_RESTART)  != 0;
  panel.temp        = (c163 & DSKY_TEMP)     != 0;
}

void
dsky_init(void)
{
  last_generation = 0;
  memset(&panel, 0, sizeof panel);
}

/* Tiny helper: draw a lamp as a filled rectangle with a label. Color depends
 * on lit state. */
static void
draw_lamp(surface_t *fb, int x, int y, const char *label, bool lit)
{
  uint32_t bg = lit ? graphics_make_color(255, 200, 40, 255)
                    : graphics_make_color(40, 40, 40, 255);
  graphics_draw_box(fb, x, y, 56, 16, bg);
  graphics_draw_text(fb, x + 3, y + 4, label);
}

void
dsky_render(surface_t *fb)
{
  /* Only re-decode if the AGC pushed something new; cheap optimisation. */
  if (g_dsky.generation != last_generation) {
    decode_panel();
    last_generation = g_dsky.generation;
  }

  graphics_fill_screen(fb, graphics_make_color(0, 0, 0, 255));
  graphics_set_color(graphics_make_color(220, 220, 220, 255),
                     graphics_make_color(0, 0, 0, 255));

  char line[32];

  /* Top row of lamps (subset; expand as more channels get wired). */
  draw_lamp(fb, 8,   8, "UPLINK", panel.uplink_acty);
  draw_lamp(fb, 72,  8, "NO ATT", panel.no_att);
  draw_lamp(fb, 136, 8, "STBY",   panel.standby);
  draw_lamp(fb, 200, 8, "KEY REL", panel.key_rel);
  draw_lamp(fb, 8,  28, "OPR ERR", panel.opr_err);
  draw_lamp(fb, 72, 28, "TEMP",    panel.temp);
  draw_lamp(fb, 136,28, "GIMBAL",  panel.gimbal_lock);
  draw_lamp(fb, 200,28, "RESTART", panel.restart);

  /* COMP ACTY indicator: small box top-left of the digit panel. */
  draw_lamp(fb, 8, 56, "COMP", panel.comp_acty);

  /* PROG/VERB/NOUN headers. */
  graphics_draw_text(fb, 80,  56, "PROG");
  graphics_draw_text(fb, 144, 56, "VERB");
  graphics_draw_text(fb, 208, 56, "NOUN");

  snprintf(line, sizeof line, "%c%c", panel.prog[0], panel.prog[1]);
  graphics_draw_text(fb,  80, 72, line);
  snprintf(line, sizeof line, "%c%c", panel.verb[0], panel.verb[1]);
  graphics_draw_text(fb, 144, 72, line);
  snprintf(line, sizeof line, "%c%c", panel.noun[0], panel.noun[1]);
  graphics_draw_text(fb, 208, 72, line);

  /* R1/R2/R3 with sign. */
  snprintf(line, sizeof line, "R1 %c%c%c%c%c%c",
           panel.s1, panel.r1[0], panel.r1[1], panel.r1[2], panel.r1[3], panel.r1[4]);
  graphics_draw_text(fb, 80,  104, line);
  snprintf(line, sizeof line, "R2 %c%c%c%c%c%c",
           panel.s2, panel.r2[0], panel.r2[1], panel.r2[2], panel.r2[3], panel.r2[4]);
  graphics_draw_text(fb, 80,  128, line);
  snprintf(line, sizeof line, "R3 %c%c%c%c%c%c",
           panel.s3, panel.r3[0], panel.r3[1], panel.r3[2], panel.r3[3], panel.r3[4]);
  graphics_draw_text(fb, 80,  152, line);

  /* Keypad hint. */
  graphics_draw_text(fb, 8, 200, "D-pad: digits  A: ENTR  B: PRO");
  graphics_draw_text(fb, 8, 212, "L: VERB  R: NOUN  Z: CLR  Start: RSET");
}
