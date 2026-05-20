#ifndef APOLLO64_INPUT_MAP_H
#define APOLLO64_INPUT_MAP_H

/*
 * Pure N64-controller -> DSKY-keycode mapping. libdragon-free so it can be
 * unit-tested on the host (see tests/input_map_test.c).
 *
 * The DSKY has 19 keys; the N64 controller has 14 digital buttons. We use a
 * single shift layer (hold Z) to reach them all - Z on its own emits
 * nothing. Every DSKY key is reachable; nothing is shadowed.
 *
 *   Button      base layer      Z held (shift)
 *   --------    ----------      --------------
 *   D-pad up    1               0
 *   D-pad down  2               + (plus)
 *   D-pad left  3               - (minus)
 *   D-pad right 4               (none)
 *   C up        5               (none)
 *   C down      6               (none)
 *   C left      7               (none)
 *   C right     8               (none)
 *   A           ENTR            PRO
 *   B           9               KEY REL
 *   L           VERB            CLR
 *   R           NOUN            (none)
 *   Start       RSET            (none)
 *   Z           (shift)         -
 */

#include <stdbool.h>
#include <stdint.h>

/* Plain digital-button snapshot. One bool per N64 button. */
typedef struct {
  bool a, b, z, start, l, r;
  bool d_up, d_down, d_left, d_right;
  bool c_up, c_down, c_left, c_right;
} dsky_buttons_t;

/* Given this frame's and last frame's button state, return the DSKY keypad
 * code for the single button that just went down (released -> pressed), or
 * DSKY_KEY_NONE if nothing edged. Z is the shift modifier and never emits
 * a code itself. At most one keycode per call. */
uint8_t input_map_edge(const dsky_buttons_t *now, const dsky_buttons_t *was);

#endif
