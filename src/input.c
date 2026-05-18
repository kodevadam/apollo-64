/*
 * apollo-64: N64 controller -> DSKY keypad mapping.
 *
 * The real DSKY has 19 keys; the N64 controller has 14 buttons. We use a
 * modal mapping: D-pad enters digits 0-9 directly (with the C-buttons
 * filling out the second hand), and the shoulder/face buttons cover the
 * action keys (VERB, NOUN, ENTR, PRO, CLR, RSET, KEY REL).
 *
 * If you wire a real DSKY into a controller port, this is where you'd
 * read it instead.
 */

#include <libdragon.h>

#include "input.h"
#include "agc_host.h"

static joypad_buttons_t prev;

void
input_init(void)
{
  joypad_init();
  prev = (joypad_buttons_t){ .raw = 0 };
}

/* Return the DSKY key code for the first button that transitioned from
 * released to pressed this frame, or DSKY_KEY_NONE if nothing edged. */
static uint8_t
map_edge(joypad_buttons_t now, joypad_buttons_t was)
{
  /* Edge detection: pressed this frame and not last frame. */
#define EDGE(field) (now.field && !was.field)

  if (EDGE(d_up))    return DSKY_KEY_1;
  if (EDGE(d_right)) return DSKY_KEY_3;
  if (EDGE(d_down))  return DSKY_KEY_5;
  if (EDGE(d_left))  return DSKY_KEY_7;
  if (EDGE(c_up))    return DSKY_KEY_2;
  if (EDGE(c_right)) return DSKY_KEY_4;
  if (EDGE(c_down))  return DSKY_KEY_6;
  if (EDGE(c_left))  return DSKY_KEY_8;
  if (EDGE(a))       return DSKY_KEY_ENTR;
  if (EDGE(b))       return DSKY_KEY_PRO;
  if (EDGE(l))       return DSKY_KEY_VERB;
  if (EDGE(r))       return DSKY_KEY_NOUN;
  if (EDGE(z))       return DSKY_KEY_CLR;
  if (EDGE(start))   return DSKY_KEY_RSET;

  /* 9 and 0 share a chord with the shoulders to avoid stealing more digit slots.
   * Chord = "L + C-up" -> 9, "L + C-down" -> 0. The chord fires once on press
   * of the C-button while L is held. */
  if (now.l && EDGE(c_up))   return DSKY_KEY_9;
  if (now.l && EDGE(c_down)) return DSKY_KEY_0;

  /* KEY REL on Start chord. */
  if (now.z && EDGE(start))  return DSKY_KEY_KEY_REL;

  /* +/- on the analog stick edges (cheap & cheerful). */
  /* (Skipped here for clarity; wire up via stick deflection thresholds.) */

#undef EDGE
  return DSKY_KEY_NONE;
}

void
input_poll(void)
{
  joypad_poll();
  joypad_buttons_t now = joypad_get_buttons(JOYPAD_PORT_1);

  uint8_t code = map_edge(now, prev);
  if (code != DSKY_KEY_NONE)
    agc_host_press_key(code);

  prev = now;
}
