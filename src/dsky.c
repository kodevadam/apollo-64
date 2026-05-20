/*
 * apollo-64: DSKY renderer. libdragon-specific. The decode logic lives in
 * dsky_decode.{c,h} so it can be unit-tested on the host.
 *
 * The numeric displays are drawn as real seven-segment digits - segments
 * painted procedurally, so there is no image-asset pipeline. Lit segments
 * glow in the DSKY's electroluminescent green; the unlit "ghost" segments
 * stay just visible, exactly like the real panel. Status lamps are the
 * familiar backlit caption tiles.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <libdragon.h>

#include "dsky.h"
#include "dsky_decode.h"
#include "agc_host.h"
#include "sound.h"

static uint32_t      last_generation;
static dsky_panel_t  panel;

/* --- palette ------------------------------------------------------------ */
static uint32_t col_bg;       /* panel background            */
static uint32_t col_seg_on;   /* lit 7-seg segment           */
static uint32_t col_seg_off;  /* unlit "ghost" segment       */
static uint32_t col_label;    /* caption text                */
static uint32_t col_lamp_on;  /* lit status lamp             */
static uint32_t col_lamp_off; /* unlit status lamp           */

void
dsky_init(void)
{
  last_generation = 0;
  memset(&panel, 0, sizeof panel);
  col_bg       = graphics_make_color(  6,   8,   6, 255);
  col_seg_on   = graphics_make_color( 90, 255, 140, 255);
  col_seg_off  = graphics_make_color( 18,  34,  22, 255);
  col_label    = graphics_make_color(190, 200, 190, 255);
  col_lamp_on  = graphics_make_color(255, 238, 170, 255);
  col_lamp_off = graphics_make_color( 34,  36,  40, 255);
}

/* --- seven-segment digit ----------------------------------------------- *
 *
 *      aaaa
 *     f    b
 *     f    b
 *      gggg
 *     e    c
 *     e    c
 *      dddd
 *
 * Segment bits: a=1 b=2 c=4 d=8 e=16 f=32 g=64.
 */
#define SEG_A 1
#define SEG_B 2
#define SEG_C 4
#define SEG_D 8
#define SEG_E 16
#define SEG_F 32
#define SEG_G 64

static uint8_t
segments_for(char ch)
{
  switch (ch) {
    case '0': return SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F;
    case '1': return SEG_B|SEG_C;
    case '2': return SEG_A|SEG_B|SEG_G|SEG_E|SEG_D;
    case '3': return SEG_A|SEG_B|SEG_G|SEG_C|SEG_D;
    case '4': return SEG_F|SEG_G|SEG_B|SEG_C;
    case '5': return SEG_A|SEG_F|SEG_G|SEG_C|SEG_D;
    case '6': return SEG_A|SEG_F|SEG_G|SEG_E|SEG_C|SEG_D;
    case '7': return SEG_A|SEG_B|SEG_C;
    case '8': return 0x7F;
    case '9': return SEG_A|SEG_B|SEG_C|SEG_D|SEG_F|SEG_G;
    default:  return 0;   /* blank */
  }
}

/* Digit cell geometry. */
#define DIG_W 18
#define DIG_H 30
#define DIG_T 4    /* segment thickness */

/* Draw one seven-segment digit at (x,y). Unlit segments are still painted
 * (dim) so a blank digit reads as a powered-but-dark cell, like the real EL
 * display. */
static void
draw_digit(surface_t *fb, int x, int y, char ch)
{
  uint8_t segs = segments_for(ch);
  int hh = (DIG_H - 3 * DIG_T) / 2;   /* vertical-segment length */

  struct { uint8_t bit; int x, y, w, h; } s[] = {
    { SEG_A, x + DIG_T,         y,                    DIG_W - 2*DIG_T, DIG_T },
    { SEG_G, x + DIG_T,         y + DIG_T + hh,        DIG_W - 2*DIG_T, DIG_T },
    { SEG_D, x + DIG_T,         y + 2*DIG_T + 2*hh,    DIG_W - 2*DIG_T, DIG_T },
    { SEG_F, x,                 y + DIG_T,             DIG_T,           hh    },
    { SEG_B, x + DIG_W - DIG_T, y + DIG_T,             DIG_T,           hh    },
    { SEG_E, x,                 y + 2*DIG_T + hh,      DIG_T,           hh    },
    { SEG_C, x + DIG_W - DIG_T, y + 2*DIG_T + hh,      DIG_T,           hh    },
  };
  for (unsigned i = 0; i < sizeof s / sizeof s[0]; i++)
    graphics_draw_box(fb, s[i].x, s[i].y, s[i].w, s[i].h,
                      (segs & s[i].bit) ? col_seg_on : col_seg_off);
}

/* Draw a sign glyph: '+', '-', or ' ' (blank), sized to the digit cell. */
static void
draw_sign(surface_t *fb, int x, int y, char sign)
{
  int cy = y + DIG_H / 2 - DIG_T / 2;
  uint32_t c = (sign == '+' || sign == '-') ? col_seg_on : col_seg_off;
  graphics_draw_box(fb, x, cy, DIG_W, DIG_T, c);          /* horizontal bar */
  if (sign == '+')
    graphics_draw_box(fb, x + DIG_W/2 - DIG_T/2, y + DIG_H/2 - DIG_W/2,
                      DIG_T, DIG_W, col_seg_on);          /* vertical bar  */
}

/* Draw a run of seven-segment digits from a string. */
static void
draw_digits(surface_t *fb, int x, int y, const char *digits, int n)
{
  for (int i = 0; i < n; i++)
    draw_digit(fb, x + i * (DIG_W + 4), y, digits[i]);
}

/* --- status lamp -------------------------------------------------------- */
#define LAMP_W 62
#define LAMP_H 20

static void
draw_lamp(surface_t *fb, int x, int y, const char *l1, const char *l2, bool lit)
{
  graphics_draw_box(fb, x, y, LAMP_W, LAMP_H, lit ? col_lamp_on : col_lamp_off);
  graphics_set_color(lit ? graphics_make_color(20, 20, 20, 255) : col_label,
                     col_bg);
  graphics_draw_text(fb, x + 4, y + 2, l1);
  if (l2) graphics_draw_text(fb, x + 4, y + 10, l2);
}

void
dsky_render(surface_t *fb)
{
  if (g_dsky.generation != last_generation) {
    dsky_decode_panel(&g_dsky, &panel);
    last_generation = g_dsky.generation;
  }

  /* The caution tone tracks the AGC's OPR ERR state - it sounds exactly
   * while a real operator-error condition is asserted, and stops when the
   * operator clears it (RSET). */
  sound_set_alarm(panel.opr_err);

  graphics_fill_screen(fb, col_bg);

  /* --- status lamp panel, left side: two columns of caption tiles --- */
  int lx = 6, rx = 6 + LAMP_W + 4, ly = 8, dy = LAMP_H + 3;
  draw_lamp(fb, lx, ly + 0*dy, "UPLINK",  "ACTY",  panel.uplink_acty);
  draw_lamp(fb, lx, ly + 1*dy, "NO ATT",  NULL,    panel.no_att);
  draw_lamp(fb, lx, ly + 2*dy, "STBY",    NULL,    panel.standby);
  draw_lamp(fb, lx, ly + 3*dy, "KEY REL", NULL,    panel.key_rel);
  draw_lamp(fb, lx, ly + 4*dy, "OPR ERR", NULL,    panel.opr_err);
  draw_lamp(fb, rx, ly + 0*dy, "TEMP",    NULL,    panel.temp);
  draw_lamp(fb, rx, ly + 1*dy, "GIMBAL",  "LOCK",  panel.gimbal_lock);
  draw_lamp(fb, rx, ly + 2*dy, "PROG",    NULL,    panel.prog_alarm);
  draw_lamp(fb, rx, ly + 3*dy, "RESTART", NULL,    panel.restart);
  draw_lamp(fb, rx, ly + 4*dy, "TRACKER", NULL,    panel.tracker);

  /* COMP ACTY tile sits below the lamp grid. */
  draw_lamp(fb, lx, ly + 5*dy, "COMP",    "ACTY",  panel.comp_acty);

  /* --- numeric display panel, right side --- */
  int px = 138;          /* left edge of the digit panel */
  graphics_set_color(col_label, col_bg);

  /* PROG: caption then two digits. */
  graphics_draw_text(fb, px, 10, "PROG");
  draw_digits(fb, px + 44, 6, panel.prog, 2);

  /* VERB / NOUN row. While the AGC is flashing verb/noun (it wants the
   * operator to act), the engine raises vn_flash during the off-phase of
   * the 1.28 s cycle - blank both fields then so they visibly flash. */
  static const char blank2[3] = "  ";
  graphics_draw_text(fb, px,       50, "VERB");
  draw_digits(fb, px + 36, 46, panel.vn_flash ? blank2 : panel.verb, 2);
  graphics_draw_text(fb, px + 90,  50, "NOUN");
  draw_digits(fb, px + 126, 46, panel.vn_flash ? blank2 : panel.noun, 2);

  /* R1 / R2 / R3: caption, sign, five digits. */
  int ry[3] = { 92, 130, 168 };
  const char *rlabel[3] = { "R1", "R2", "R3" };
  const char *rdig[3]   = { panel.r1, panel.r2, panel.r3 };
  char  rsign[3]        = { panel.s1, panel.s2, panel.s3 };
  for (int r = 0; r < 3; r++) {
    graphics_set_color(col_label, col_bg);
    graphics_draw_text(fb, px, ry[r] + 10, rlabel[r]);
    draw_sign(fb, px + 20, ry[r], rsign[r]);
    draw_digits(fb, px + 20 + DIG_W + 2, ry[r], rdig[r], 5);
  }

  /* Controller hint along the bottom (kept within the 320px width). */
  graphics_set_color(col_label, col_bg);
  graphics_draw_text(fb,  6, 214, "Keys: D-pad/C digits, L VERB, R NOUN");
  graphics_draw_text(fb,  6, 224, "A ENTR, Start RSET, hold Z for shift");
}
