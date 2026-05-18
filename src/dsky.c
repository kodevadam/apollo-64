/*
 * apollo-64: DSKY renderer. libdragon-specific. The decode logic lives in
 * dsky_decode.{c,h} so it can be unit-tested on the host.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <libdragon.h>

#include "dsky.h"
#include "dsky_decode.h"
#include "agc_host.h"

static uint32_t      last_generation;
static dsky_panel_t  panel;

void
dsky_init(void)
{
  last_generation = 0;
  memset(&panel, 0, sizeof panel);
}

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
  if (g_dsky.generation != last_generation) {
    dsky_decode_panel(&g_dsky, &panel);
    last_generation = g_dsky.generation;
  }

  graphics_fill_screen(fb, graphics_make_color(0, 0, 0, 255));
  graphics_set_color(graphics_make_color(220, 220, 220, 255),
                     graphics_make_color(0, 0, 0, 255));

  char line[32];

  draw_lamp(fb,   8,  8, "UPLINK",  panel.uplink_acty);
  draw_lamp(fb,  72,  8, "NO ATT",  panel.no_att);
  draw_lamp(fb, 136,  8, "STBY",    panel.standby);
  draw_lamp(fb, 200,  8, "KEY REL", panel.key_rel);
  draw_lamp(fb,   8, 28, "OPR ERR", panel.opr_err);
  draw_lamp(fb,  72, 28, "TEMP",    panel.temp);
  draw_lamp(fb, 136, 28, "GIMBAL",  panel.gimbal_lock);
  draw_lamp(fb, 200, 28, "RESTART", panel.restart);

  draw_lamp(fb, 8, 56, "COMP", panel.comp_acty);

  graphics_draw_text(fb,  80, 56, "PROG");
  graphics_draw_text(fb, 144, 56, "VERB");
  graphics_draw_text(fb, 208, 56, "NOUN");

  snprintf(line, sizeof line, "%c%c", panel.prog[0], panel.prog[1]);
  graphics_draw_text(fb,  80, 72, line);
  snprintf(line, sizeof line, "%c%c", panel.verb[0], panel.verb[1]);
  graphics_draw_text(fb, 144, 72, line);
  snprintf(line, sizeof line, "%c%c", panel.noun[0], panel.noun[1]);
  graphics_draw_text(fb, 208, 72, line);

  snprintf(line, sizeof line, "R1 %c%c%c%c%c%c",
           panel.s1, panel.r1[0], panel.r1[1], panel.r1[2], panel.r1[3], panel.r1[4]);
  graphics_draw_text(fb, 80, 104, line);
  snprintf(line, sizeof line, "R2 %c%c%c%c%c%c",
           panel.s2, panel.r2[0], panel.r2[1], panel.r2[2], panel.r2[3], panel.r2[4]);
  graphics_draw_text(fb, 80, 128, line);
  snprintf(line, sizeof line, "R3 %c%c%c%c%c%c",
           panel.s3, panel.r3[0], panel.r3[1], panel.r3[2], panel.r3[3], panel.r3[4]);
  graphics_draw_text(fb, 80, 152, line);

  graphics_draw_text(fb, 8, 200, "D-pad: digits  A: ENTR  B: PRO");
  graphics_draw_text(fb, 8, 212, "L: VERB  R: NOUN  Z: CLR  Start: RSET");
}
