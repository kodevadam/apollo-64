/*
 * apollo-64: pure controller -> DSKY keycode mapping. See input_map.h for
 * the button layout. libdragon-free; input.c does the joypad_buttons_t ->
 * dsky_buttons_t translation and calls in here.
 */

#include "input_map.h"
#include "input.h"

uint8_t
input_map_edge(const dsky_buttons_t *now, const dsky_buttons_t *was)
{
  /* Edge = down this frame, up last frame. */
#define EDGE(field) (now->field && !was->field)

  bool shift = now->z;

  /* D-pad: digits 1-4, or 0/+/- in the shift layer. */
  if (EDGE(d_up))    return shift ? DSKY_KEY_0     : DSKY_KEY_1;
  if (EDGE(d_down))  return shift ? DSKY_KEY_PLUS  : DSKY_KEY_2;
  if (EDGE(d_left))  return shift ? DSKY_KEY_MINUS : DSKY_KEY_3;
  if (EDGE(d_right)) return DSKY_KEY_4;

  /* C-buttons: digits 5-8 (no shift alternates). */
  if (EDGE(c_up))    return DSKY_KEY_5;
  if (EDGE(c_down))  return DSKY_KEY_6;
  if (EDGE(c_left))  return DSKY_KEY_7;
  if (EDGE(c_right)) return DSKY_KEY_8;

  /* Face / shoulder / start: action keys, with shift alternates. */
  if (EDGE(a))     return shift ? DSKY_KEY_PRO     : DSKY_KEY_ENTR;
  if (EDGE(b))     return shift ? DSKY_KEY_KEY_REL : DSKY_KEY_9;
  if (EDGE(l))     return shift ? DSKY_KEY_CLR     : DSKY_KEY_VERB;
  if (EDGE(r))     return DSKY_KEY_NOUN;
  if (EDGE(start)) return DSKY_KEY_RSET;

  /* Z is the shift modifier - it never emits a key on its own. */

#undef EDGE
  return DSKY_KEY_NONE;
}
