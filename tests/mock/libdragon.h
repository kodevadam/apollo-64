/*
 * Minimal mock of the libdragon joypad API, just enough to compile and
 * unit-test src/input.c on the host. Only the joypad surface input.c
 * touches is provided.
 *
 * The real libdragon joypad_buttons_t is a union of a uint16 and a packed
 * bitfield struct; input.c only ever uses the *named* fields, so a plain
 * struct with the same field names exercises read_buttons() faithfully -
 * the bit layout is irrelevant to named access.
 *
 * tests/input_poll_test.c sets mock_joypad_state to whatever it wants
 * joypad_get_buttons() to return.
 */
#ifndef APOLLO64_MOCK_LIBDRAGON_H
#define APOLLO64_MOCK_LIBDRAGON_H

#include <stdint.h>

typedef struct {
  unsigned a, b, z, start, l, r;
  unsigned d_up, d_down, d_left, d_right;
  unsigned c_up, c_down, c_left, c_right;
} joypad_buttons_t;

typedef int joypad_port_t;
#define JOYPAD_PORT_1 0

/* The test controls this; joypad_get_buttons() returns it. */
extern joypad_buttons_t mock_joypad_state;

static inline void joypad_init(void) {}
static inline void joypad_poll(void) {}
static inline joypad_buttons_t joypad_get_buttons(joypad_port_t port) {
  (void)port;
  return mock_joypad_state;
}

#endif
