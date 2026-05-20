/*
 * apollo-64: N64 controller -> DSKY. Reads the joypad via libdragon, then
 * defers the actual button-to-keycode mapping to input_map.c (which is
 * libdragon-free and unit-tested). See input_map.h for the button layout.
 *
 * If you wire a real DSKY into a controller port, this is where you'd
 * read it instead.
 */

#include <libdragon.h>

#include "input.h"
#include "input_map.h"
#include "agc_host.h"
#include "sound.h"

static dsky_buttons_t prev;

void
input_init(void)
{
  joypad_init();
  prev = (dsky_buttons_t){0};
}

/* Snapshot libdragon's joypad state into our libdragon-free struct. */
static dsky_buttons_t
read_buttons(void)
{
  joypad_buttons_t j = joypad_get_buttons(JOYPAD_PORT_1);
  return (dsky_buttons_t){
    .a = j.a, .b = j.b, .z = j.z, .start = j.start, .l = j.l, .r = j.r,
    .d_up = j.d_up, .d_down = j.d_down, .d_left = j.d_left, .d_right = j.d_right,
    .c_up = j.c_up, .c_down = j.c_down, .c_left = j.c_left, .c_right = j.c_right,
  };
}

void
input_poll(void)
{
  joypad_poll();
  dsky_buttons_t now = read_buttons();

  uint8_t code = input_map_edge(&now, &prev);
  if (code != DSKY_KEY_NONE && code != DSKY_KEY_PRO) {
    agc_host_press_key(code);
    sound_key_click();
  }

  /* PROCEED is not a keypad key - it is a held discrete on channel 032.
   * Drive it by level (Z-shift + A held), not by edge. */
  agc_host_set_pro(now.z && now.a);

  prev = now;
}
