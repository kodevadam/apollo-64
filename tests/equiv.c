/*
 * apollo-64: AGC equivalence / determinism harness.
 *
 * Runs a scripted operator scenario against the AGC and fingerprints the
 * complete machine state at fixed checkpoints. The fingerprint is a hash
 * over all of erasable memory, every I/O channel, and the cycle counter -
 * i.e. everything that defines the AGC's state.
 *
 * Two purposes:
 *
 *  1. Determinism / regression net. The AGC is a pure integer machine;
 *     given the same ROM and the same scripted input it must reach
 *     bit-identical state every run, on any host. The expected
 *     fingerprints are committed below; CI re-runs and asserts they are
 *     unchanged, so any accidental change in AGC behaviour is caught.
 *
 *  2. The reference side of an upstream cross-check. Because apollo-64
 *     runs the *unmodified* yaAGC engine, the same scenario driven into a
 *     stock yaAGC build must produce the same fingerprints. The word-wise
 *     hash is endianness-independent so the numbers are portable. See
 *     docs/EQUIVALENCE.md for the cross-check procedure.
 *
 * Build/run: make -C tests run-equiv
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/agc_host.h"
#include "../vendor/yaAGC/agc_engine.h"

/* DSKY keypad codes (see src/input.h). */
enum { K_0=020, K_1=001, K_3=003, K_5=005, K_6=006,
       K_VERB=021, K_NOUN=037, K_ENTR=034, K_RSET=022 };

/* --- scripted scenario ----------------------------------------------- *
 * A scenario is a list of (cycle, keycode). The chosen script boots the
 * AGC, clears restart, runs the V35E lamp test, then sets up the V16 N36
 * clock monitor - a representative spread of verb/noun/data activity. */
typedef struct { unsigned long cycle; uint8_t key; } event_t;

static const event_t scenario[] = {
  { 1500000, K_RSET },
  /* V35E - lamp test */
  { 2000000, K_VERB }, { 2300000, K_3 }, { 2600000, K_5 }, { 2900000, K_ENTR },
  /* V16 N36 E - monitor the AGC clock */
  { 4000000, K_VERB }, { 4300000, K_1 }, { 4600000, K_6 },
  { 4900000, K_NOUN }, { 5200000, K_3 }, { 5500000, K_6 },
  { 5800000, K_ENTR },
};
#define NEVENTS (sizeof scenario / sizeof scenario[0])

/* Checkpoints: cycle counts at which to fingerprint the machine. */
static const unsigned long checkpoint[] = {
  8000000, 16000000, 24000000, 32000000, 40000000,
};
#define NCHECK (sizeof checkpoint / sizeof checkpoint[0])

/* Expected fingerprints. Filled in from a first run; CI asserts these.
 * A change here means AGC behaviour changed - intentional or not. */
static const uint32_t golden[NCHECK] = {
  0xe5a92fbc, 0x92105968, 0xf4b83632, 0x34a4768a, 0xc36db2af,
};

/* Word-wise FNV-1a so the hash does not depend on host endianness. */
static uint32_t
mix(uint32_t h, uint32_t v)
{
  h ^= (v & 0xffff);
  h *= 16777619u;
  return h;
}

static uint32_t
fingerprint(void)
{
  uint32_t h = 2166136261u;
  for (int b = 0; b < 8; b++)
    for (int i = 0; i < 0400; i++)
      h = mix(h, (uint16_t)g_agc.Erasable[b][i]);
  for (int c = 0; c < NUM_CHANNELS; c++)
    h = mix(h, (uint16_t)g_agc.InputChannel[c]);
  for (int i = 0; i < 16; i++)
    h = mix(h, (uint16_t)g_agc.OutputChannel10[i]);
  h = mix(h, (uint16_t)g_agc.OutputChannel7);
  h = mix(h, (uint32_t)(g_agc.CycleCounter & 0xffff));
  h = mix(h, (uint32_t)((g_agc.CycleCounter >> 16) & 0xffff));
  return h;
}

int
main(int argc, char **argv)
{
  int regen = (argc > 1 && !strcmp(argv[1], "regen"));

  printf("apollo-64 AGC equivalence / determinism harness\n");
  printf("===============================================\n");

  agc_host_init();

  const unsigned long batch = 100000ul;
  unsigned ev = 0, ck = 0;
  int failures = 0;
  uint32_t got[NCHECK] = {0};

  for (unsigned long done = 0; ck < NCHECK; done += batch) {
    while (ev < NEVENTS && done >= scenario[ev].cycle) {
      agc_host_press_key(scenario[ev].key);
      ev++;
    }
    agc_host_tick((uint32_t)batch);
    if (done + batch == checkpoint[ck]) {
      got[ck] = fingerprint();
      printf("  checkpoint %lu cycles: fingerprint 0x%08x",
             checkpoint[ck], got[ck]);
      if (!regen) {
        if (got[ck] == golden[ck]) {
          printf("  OK\n");
        } else {
          printf("  MISMATCH (expected 0x%08x)\n", golden[ck]);
          failures++;
        }
      } else {
        printf("\n");
      }
      ck++;
    }
  }

  if (regen) {
    printf("\nGolden values (paste into the golden[] array):\n  ");
    for (unsigned i = 0; i < NCHECK; i++)
      printf("0x%08x, ", got[i]);
    printf("\n");
    return 0;
  }

  if (failures == 0) {
    printf("\nPASS: all %u checkpoints match - AGC state is deterministic\n"
           "      and unchanged from the committed reference.\n", (unsigned)NCHECK);
    return 0;
  }
  printf("\nFAIL: %d checkpoint(s) diverged from the committed reference.\n",
         failures);
  return 1;
}
