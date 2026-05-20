/*
 * Unit tests for src/input_map.c - the N64 controller -> DSKY keycode
 * mapping. No libdragon needed; pure logic.
 *
 * Run via: make -C tests run-input-test
 */

#include <stdio.h>
#include <string.h>

#include "../src/input_map.h"
#include "../src/input.h"

static int failures = 0;

#define EXPECT(name, actual, expected) do {                       \
  if ((actual) != (expected)) {                                   \
    printf("  FAIL: %s: got %03o, expected %03o\n",                \
           name, (unsigned)(actual), (unsigned)(expected));        \
    failures++;                                                   \
  }                                                               \
} while (0)

/* Press `field` (with optional shift via z): build now/was so exactly
 * that button edges down, run the mapper, return the code. */
#define PRESS(field, with_z) ({                                   \
  dsky_buttons_t was = {0};                                       \
  dsky_buttons_t now = {0};                                       \
  now.field = true;                                               \
  now.z = (with_z);                                               \
  input_map_edge(&now, &was);                                     \
})

static void
test_base_layer(void)
{
  printf("test_base_layer:\n");
  EXPECT("d_up=1",    PRESS(d_up,    false), DSKY_KEY_1);
  EXPECT("d_down=2",  PRESS(d_down,  false), DSKY_KEY_2);
  EXPECT("d_left=3",  PRESS(d_left,  false), DSKY_KEY_3);
  EXPECT("d_right=4", PRESS(d_right, false), DSKY_KEY_4);
  EXPECT("c_up=5",    PRESS(c_up,    false), DSKY_KEY_5);
  EXPECT("c_down=6",  PRESS(c_down,  false), DSKY_KEY_6);
  EXPECT("c_left=7",  PRESS(c_left,  false), DSKY_KEY_7);
  EXPECT("c_right=8", PRESS(c_right, false), DSKY_KEY_8);
  EXPECT("b=9",       PRESS(b,       false), DSKY_KEY_9);
  EXPECT("a=ENTR",    PRESS(a,       false), DSKY_KEY_ENTR);
  EXPECT("l=VERB",    PRESS(l,       false), DSKY_KEY_VERB);
  EXPECT("r=NOUN",    PRESS(r,       false), DSKY_KEY_NOUN);
  EXPECT("start=RSET",PRESS(start,   false), DSKY_KEY_RSET);
}

static void
test_shift_layer(void)
{
  printf("test_shift_layer:\n");
  /* Z held -> shift alternates. Note: with_z=true means now.z is set,
   * but the edge is on the named field, and Z itself emits nothing. */
  EXPECT("Z+d_up=0",      PRESS(d_up,   true), DSKY_KEY_0);
  EXPECT("Z+d_down=+",    PRESS(d_down, true), DSKY_KEY_PLUS);
  EXPECT("Z+d_left=-",    PRESS(d_left, true), DSKY_KEY_MINUS);
  EXPECT("Z+b=KEY REL",   PRESS(b,      true), DSKY_KEY_KEY_REL);
  EXPECT("Z+l=CLR",       PRESS(l,      true), DSKY_KEY_CLR);
  EXPECT("Z+a=PRO",       PRESS(a,      true), DSKY_KEY_PRO);
}

static void
test_z_emits_nothing(void)
{
  printf("test_z_emits_nothing:\n");
  dsky_buttons_t was = {0};
  dsky_buttons_t now = {0};
  now.z = true;  /* Z pressed, nothing else */
  EXPECT("Z alone -> NONE", input_map_edge(&now, &was), DSKY_KEY_NONE);
}

static void
test_no_edge_no_key(void)
{
  printf("test_no_edge_no_key:\n");
  /* Button held across both frames -> no edge -> no key. */
  dsky_buttons_t held = {0};
  held.a = true;
  EXPECT("held A -> NONE", input_map_edge(&held, &held), DSKY_KEY_NONE);
  /* Nothing pressed at all. */
  dsky_buttons_t none = {0};
  EXPECT("idle -> NONE", input_map_edge(&none, &none), DSKY_KEY_NONE);
}

static void
test_chords_reachable(void)
{
  printf("test_chords_reachable:\n");
  /* The old code shadowed 9/0/KEY-REL behind plain-button checks. Verify
   * the shift-layer keys are genuinely reachable - i.e. holding Z first,
   * then edging the second button, yields the alternate, not the base. */
  dsky_buttons_t was = {0};
  was.z = true;                 /* Z already held last frame */
  dsky_buttons_t now = was;
  now.b = true;                 /* B edges down this frame */
  EXPECT("Z(held)+B = KEY REL", input_map_edge(&now, &was), DSKY_KEY_KEY_REL);

  was = (dsky_buttons_t){0};
  was.z = true;
  now = was;
  now.d_up = true;
  EXPECT("Z(held)+d_up = 0", input_map_edge(&now, &was), DSKY_KEY_0);
}

int
main(void)
{
  test_base_layer();
  test_shift_layer();
  test_z_emits_nothing();
  test_no_edge_no_key();
  test_chords_reachable();
  if (failures == 0) {
    printf("\nAll input-map tests passed.\n");
    return 0;
  }
  printf("\n%d test failure(s).\n", failures);
  return 1;
}
