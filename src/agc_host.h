/*
 * apollo-64: host glue between the yaAGC engine and the N64.
 *
 * The engine runs as a pure CPU simulator (see vendor/yaAGC/agc_engine.c).
 * Everything outside the CPU - DSKY display, keypad, IMU counters, alarms -
 * goes through the I/O channel callbacks defined here.
 *
 * Channel reference: https://www.ibiblio.org/apollo/developer.html
 */
#ifndef APOLLO64_AGC_HOST_H
#define APOLLO64_AGC_HOST_H

#include <stdbool.h>
#include <stdint.h>

#include "../vendor/yaAGC/agc_engine.h"

#define AGC_FIXED_BANKS   044   /* 36 */
#define AGC_BANK_WORDS    02000 /* 1024 */
#define AGC_CORE_ROPE_WORDS (AGC_FIXED_BANKS * AGC_BANK_WORDS)

/* Snapshot of DSKY-visible AGC state, refreshed on every channel write to 010,
 * 011, or 0163. The renderer reads this and never touches the agc_t struct.
 * Indexed by latch row (0..15). Row 12 contains the lamp word. Rows 1..11
 * carry digit/sign data per the AGC DSKY hardware spec. */
typedef struct {
  int16_t latch[16];        /* OutputChannel10[16] mirror */
  int16_t channel11;        /* Status/program lamps (channel 011) */
  int16_t channel13;        /* DSKY brightness + misc (channel 013) */
  int16_t channel163;       /* Synthesised DSKY status (TEMP, WARN, ...) */
  uint32_t generation;      /* Bumped on any DSKY-relevant write */
} dsky_snapshot_t;

/* The single AGC instance. Public so the timer ISR can call agc_engine() on it
 * without an extra indirection. */
extern agc_t g_agc;

/* Live DSKY snapshot. Updated from ChannelOutput; read by dsky_render(). */
extern volatile dsky_snapshot_t g_dsky;

/* One-shot init: zero the agc_t, load CoreRope into State.Fixed[][] using the
 * 2,3,0,1,4..35 bank reordering, set RegZ to 04000 (the AGC boot vector), and
 * initialise IO channel defaults the way agc_engine_init.c would on a host
 * build. Must be called before the first agc_engine() tick. */
void agc_host_init(void);

/* Inject a DSKY keypress. `key_code` is the 5-bit DSKY keypad code (see
 * src/input.c for the constants). 0 means "key released". This sets bits 4:0
 * of input channel 015 and raises the keystroke interrupt request. */
void agc_host_press_key(uint8_t key_code);

/* Step the AGC by `cycles` machine cycles. One AGC machine cycle is ~11.7 us
 * of wall time. Call this from a timer ISR at the right rate to maintain
 * real-time fidelity, or from the main loop for free-running mode. */
void agc_host_tick(uint32_t cycles);

/* Toggle the simulated IMU peripherals (PIPA pulse generation). On by
 * default. Useful for the smoke test to compare with/without peripherals. */
void agc_host_set_peripherals(bool on);

#endif
