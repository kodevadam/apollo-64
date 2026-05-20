/*
 * End-to-end test of src/input.c's input_poll() - the full controller
 * chain: joypad read -> read_buttons() field copy -> input_map_edge() ->
 * agc_host_press_key / agc_host_set_pro.
 *
 * input.c is compiled against tests/mock/libdragon.h (a stub joypad) and
 * the agc_host entry points are stubbed here to record what input.c
 * calls. This catches mistakes the pure input_map_test can't - in
 * particular a wrong field copy in read_buttons() (e.g. d_up <- d_down).
 *
 * Run via: make -C tests run-input-poll-test
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "mock/libdragon.h"
#include "../src/input.h"

/* --- mock state the harness drives --- */
joypad_buttons_t mock_joypad_state;

/* --- stubs recording what input.c asked the AGC to do --- */
static uint8_t last_key   = DSKY_KEY_NONE;
static int     key_calls  = 0;
static int     pro_held   = -1;   /* -1 = never called */

void agc_host_press_key(uint8_t code) { last_key = code; key_calls++; }
void agc_host_set_pro(bool held)      { pro_held = held ? 1 : 0; }

/* input.c clicks the speaker on each keystroke; stub it for the test. */
static int click_calls = 0;
void sound_key_click(void) { click_calls++; }

static int failures = 0;
#define CHECK(name, cond) do {                              \
  if (!(cond)) { printf("  FAIL: %s\n", name); failures++; } \
} while (0)

/* Reset recorders, then poll once with the given button state. The first
 * poll after init sees a fresh-vs-zero edge. */
static void
poll_with(joypad_buttons_t state)
{
  last_key = DSKY_KEY_NONE;
  key_calls = 0;
  click_calls = 0;
  mock_joypad_state = state;
  input_poll();
}

int
main(void)
{
  input_init();

  printf("test_single_buttons:\n");
  /* L -> VERB */
  poll_with((joypad_buttons_t){ .l = 1 });
  CHECK("L press -> one key call", key_calls == 1);
  CHECK("L press -> VERB",         last_key == DSKY_KEY_VERB);
  CHECK("L press -> one click",    click_calls == 1);
  /* release everything: no edge, no key */
  poll_with((joypad_buttons_t){0});
  CHECK("release -> no key", key_calls == 0);

  /* D-pad up -> digit 1 (verifies read_buttons copies d_up, not d_down) */
  poll_with((joypad_buttons_t){ .d_up = 1 });
  CHECK("d_up -> '1'", last_key == DSKY_KEY_1);
  poll_with((joypad_buttons_t){0});

  /* C-down -> digit 6 (verifies the c_* copies) */
  poll_with((joypad_buttons_t){ .c_down = 1 });
  CHECK("c_down -> '6'", last_key == DSKY_KEY_6);
  poll_with((joypad_buttons_t){0});

  /* Start -> RSET */
  poll_with((joypad_buttons_t){ .start = 1 });
  CHECK("start -> RSET", last_key == DSKY_KEY_RSET);
  poll_with((joypad_buttons_t){0});

  printf("test_shift_layer:\n");
  /* Z held first (no key), then B edges -> KEY REL */
  poll_with((joypad_buttons_t){ .z = 1 });
  CHECK("Z alone -> no key", key_calls == 0);
  poll_with((joypad_buttons_t){ .z = 1, .b = 1 });
  CHECK("Z+B -> KEY REL", last_key == DSKY_KEY_KEY_REL);
  poll_with((joypad_buttons_t){0});

  printf("test_proceed_level:\n");
  /* PRO is level-driven: Z+A held asserts PRO, releasing clears it. */
  pro_held = -1;
  poll_with((joypad_buttons_t){ .z = 1, .a = 1 });
  CHECK("Z+A -> PRO asserted", pro_held == 1);
  poll_with((joypad_buttons_t){0});
  CHECK("release -> PRO cleared", pro_held == 0);

  printf("test_no_double_fire:\n");
  /* A held across two polls -> exactly one ENTR. */
  joypad_buttons_t a_held = { .a = 1 };
  poll_with(a_held);
  CHECK("A first poll -> ENTR", last_key == DSKY_KEY_ENTR && key_calls == 1);
  last_key = DSKY_KEY_NONE; key_calls = 0;
  mock_joypad_state = a_held;
  input_poll();
  CHECK("A held second poll -> no repeat", key_calls == 0);

  if (failures == 0) {
    printf("\nAll input_poll tests passed.\n");
    return 0;
  }
  printf("\n%d test failure(s).\n", failures);
  return 1;
}
