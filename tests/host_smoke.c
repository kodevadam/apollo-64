/*
 * Host-side smoke test for the apollo-64 pipeline.
 *
 * Boots the AGC from the embedded CoreRope (src/rope.c) using the same
 * agc_host_init() code path the N64 build uses, runs the engine for a
 * configurable number of machine cycles, and dumps:
 *   - how many channel 010 writes happened
 *   - the final DSKY panel state, decoded
 *   - the program counter, time registers, and a few other key bits
 *
 * Intent: verify that the engine actually runs real Luminary099 code
 * without panicking, and that our channel-decode pipeline produces
 * sensible-looking output (PROG/VERB/NOUN populated with digits, not
 * gibberish). This is a smoke test, not a correctness test - the AGC
 * boot sequence depends on peripheral interrupts we haven't wired up
 * yet, so we'll see it sit in standby/restart loops.
 *
 * Build: see tests/Makefile.
 * Run:   ./tests/host_smoke [cycles]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/agc_host.h"
#include "../src/dsky_decode.h"
#include "../vendor/yaAGC/agc_engine.h"

/* Engine globals we want to twiddle for visibility. */
extern int ShowAlarms;
extern int InhibitAlarms;

/* Instrument ChannelOutput to count writes. The real ChannelOutput in
 * agc_host.c will still fire (we're linking it normally); this just adds
 * an observation hook via a wrapper symbol. Simplest approach: replace
 * the engine's call with our own by linker order.
 *
 * Actually simpler: just poll g_dsky.generation, which agc_host.c bumps on
 * every DSKY-relevant write. That's a perfectly good proxy. */

static void
print_panel(const dsky_panel_t *p)
{
  printf("    PROG=%c%c  VERB=%c%c  NOUN=%c%c\n",
         p->prog[0], p->prog[1], p->verb[0], p->verb[1],
         p->noun[0], p->noun[1]);
  printf("    R1=%c%c%c%c%c%c\n", p->s1,
         p->r1[0], p->r1[1], p->r1[2], p->r1[3], p->r1[4]);
  printf("    R2=%c%c%c%c%c%c\n", p->s2,
         p->r2[0], p->r2[1], p->r2[2], p->r2[3], p->r2[4]);
  printf("    R3=%c%c%c%c%c%c\n", p->s3,
         p->r3[0], p->r3[1], p->r3[2], p->r3[3], p->r3[4]);
  printf("    lamps: COMP=%d UPLINK=%d NO_ATT=%d STBY=%d KEY_REL=%d\n",
         p->comp_acty, p->uplink_acty, p->no_att, p->standby, p->key_rel);
  printf("           OPR_ERR=%d TEMP=%d RESTART=%d ALARM=%d\n",
         p->opr_err, p->temp, p->restart, p->prog_alarm);
}

int
main(int argc, char **argv)
{
  unsigned long cycles = (argc > 1) ? strtoul(argv[1], NULL, 0) : 200000;

  /* Optional flags (positional after cycle count, combinable). */
  bool send_v35e = false;
  bool send_v16 = false;
  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "noperipherals") == 0)
      agc_host_set_peripherals(false);
    else if (strcmp(argv[i], "alarms") == 0)
      ShowAlarms = 1;
    else if (strcmp(argv[i], "inhibit") == 0)
      InhibitAlarms = 1;
    else if (strcmp(argv[i], "v35e") == 0)
      send_v35e = true;
    else if (strcmp(argv[i], "v16n36e") == 0)
      send_v16 = true;
    else if (strncmp(argv[i], "trace=", 6) == 0) {
      g_agc_trace = fopen(argv[i] + 6, "w");
      if (!g_agc_trace) perror(argv[i] + 6);
      else fprintf(g_agc_trace, "# apollo-64 trace, t=AGC cycles since boot\n");
    }
  }

  printf("apollo-64 host smoke test\n");
  printf("=========================\n");
  printf("Loading CoreRope (%d words) and initialising AGC...\n",
         AGC_CORE_ROPE_WORDS);

  agc_host_init();

  printf("After init:\n");
  printf("  Z (PC)        = %06o (expected 04000)\n", g_agc.Erasable[0][RegZ]);
  printf("  CycleCounter  = %lu\n", (unsigned long)g_agc.CycleCounter);
  printf("  channel 030   = %06o (expected 037777)\n",
         (unsigned short)g_agc.InputChannel[030]);

  /* Quick rope sanity check: word at boot vector (bank 2, offset 0). */
  printf("  Fixed[2][0]   = %06o (boot vector instruction)\n",
         (unsigned short)g_agc.Fixed[2][0]);

  uint32_t gen0 = g_dsky.generation;
  printf("\nRunning %lu machine cycles...\n", cycles);

  /* Optional: simulate an RSET press partway through if the user passes a
   * second arg "rset". Without it the AGC sits in restart forever, which is
   * the expected behaviour but boring to look at. */
  bool send_rset = (argc > 2 && strcmp(argv[2], "rset") == 0);
  unsigned long rset_at = 200000;  /* ~2.3s simulated - well after init */
  bool rset_sent = false;

  /* Run in batches so we can show progress for very long runs. */
  unsigned long batch = (cycles > 50000) ? 50000 : cycles;
  unsigned long done = 0;
  /* Bucket the PC by 04000-octal-wide ranges to see what code areas the
   * AGC visits. Stash one sample per outer iteration; not statistically
   * pure but enough to spot "stuck in a loop" patterns. */
  unsigned long pc_buckets[16] = {0};

  while (done < cycles) {
    unsigned long n = (cycles - done < batch) ? cycles - done : batch;
    agc_host_tick((uint32_t)n);
    done += n;
    int bucket = (g_agc.Erasable[0][RegZ] >> 9) & 0xF;  /* 0512 octal per bucket */
    pc_buckets[bucket]++;
    if (send_rset && !rset_sent && done >= rset_at) {
      printf("  *** simulating RSET keypress @ cycle %lu, "
             "RestartLight pre=%u\n", done, g_agc.RestartLight);
      agc_host_press_key(022);  /* DSKY_KEY_RSET */
      /* Also directly clear the restart light - the real hardware grounds
       * the flip-flop on RSET press without needing software involvement;
       * our channel-write-trigger path requires the AGC's PINBALL handler
       * to actually run and echo, which may not happen reliably. */
      g_agc.RestartLight = 0;
      rset_sent = true;
    }

    /* V35E (lamp test). Four keypresses, plenty of time between for
     * the AGC's KEYRUPT handler to process each (and now also the
     * synthetic key-release we emit a few cycles after each press).
     * Codes are keypad-input codes per the PINBALL CHARIN2 dispatch
     * table (PINBALL_GAME__BUTTONS_AND_LIGHTS.agc:494).
     *
     * After E, VBTSTLTS fills DSPTAB with "all 8s and +" and DSPOUT
     * (T4RUPT) pushes it to channel 010 over the next ~10ms simulated.
     * The test lights stay on for 5s simulated (~425k cycles) before
     * TSTLTS2 blanks them, so snapshot somewhere between cycles 2.2M
     * and 2.5M to catch it. */
    static const uint8_t v35e_seq[] = { 021, 003, 005, 034 };  /* VERB 3 5 ENTR */
    static int v35e_step = 0;
    if (send_v35e && v35e_step < 4 && done >= 1500000 + 200000UL * v35e_step) {
      printf("  *** V35E step %d: keycode %02o @ cycle %lu\n",
             v35e_step, v35e_seq[v35e_step], done);
      agc_host_press_key(v35e_seq[v35e_step]);
      v35e_step++;
    }

    /* V16 N36 E - "monitor decimal, noun 36 (AGC clock)". Unlike the
     * one-shot lamp test, V16 is a *monitor* verb: the AGC re-displays
     * the value continuously, so R1/R2/R3 should show the time and
     * tick upward. Seven keystrokes: VERB 1 6 NOUN 3 6 ENTR. */
    static const uint8_t v16_seq[] = { 021, 001, 006, 037, 003, 006, 034 };
    static int v16_step = 0;
    if (send_v16 && v16_step < 7 && done >= 1500000 + 300000UL * v16_step) {
      printf("  *** V16N36E step %d: keycode %02o @ cycle %lu\n",
             v16_step, v16_seq[v16_step], done);
      agc_host_press_key(v16_seq[v16_step]);
      v16_step++;
    }
    printf("  ... %lu / %lu cycles, gen=%u, Z=%06o, T1=%06o, "
           "ch11=%06o ch163=%06o RESTART=%u\n",
           done, cycles,
           g_dsky.generation,
           g_agc.Erasable[0][RegZ],
           (unsigned short)g_agc.Erasable[0][RegTIME1],
           (unsigned short)g_dsky.channel11,
           (unsigned short)g_dsky.channel163,
           g_agc.RestartLight);
  }

  printf("\nFinal state:\n");
  printf("  CycleCounter      = %lu\n", (unsigned long)g_agc.CycleCounter);
  printf("  Z (PC)            = %06o\n", g_agc.Erasable[0][RegZ]);
  printf("  TIME1             = %06o\n", (unsigned short)g_agc.Erasable[0][RegTIME1]);
  printf("  TIME2             = %06o\n", (unsigned short)g_agc.Erasable[0][RegTIME2]);
  printf("  DSKY generations  = %u  (delta %u)\n",
         g_dsky.generation, g_dsky.generation - gen0);
  printf("  channel 011       = %06o\n", (unsigned short)g_dsky.channel11);
  printf("  channel 0163      = %06o\n", (unsigned short)g_dsky.channel163);
  printf("  AllowInterrupt    = %u\n", g_agc.AllowInterrupt);
  printf("  Standby           = %u\n", g_agc.Standby);
  printf("  RestartLight      = %u\n", g_agc.RestartLight);
  printf("  NightWatchman/Trp = %u / %u\n", g_agc.NightWatchman, g_agc.NightWatchmanTripped);
  printf("  RuptLock/NoRupt   = %u / %u\n", g_agc.RuptLock, g_agc.NoRupt);
  printf("  TCTrap/NoTC       = %u / %u\n", g_agc.TCTrap, g_agc.NoTC);
  printf("  WarningFilter     = %u\n", g_agc.WarningFilter);
  printf("  GeneratedWarning  = %u\n", g_agc.GeneratedWarning);
  printf("  ParityFail        = %u\n", g_agc.ParityFail);
  printf("  CheckParity       = %u\n", g_agc.CheckParity);
  printf("  PIPAX/Y/Z         = %06o / %06o / %06o\n",
         (unsigned short)g_agc.Erasable[0][RegPIPAX],
         (unsigned short)g_agc.Erasable[0][RegPIPAY],
         (unsigned short)g_agc.Erasable[0][RegPIPAZ]);
  printf("  CDUX/Y/Z          = %06o / %06o / %06o\n",
         (unsigned short)g_agc.Erasable[0][RegCDUX],
         (unsigned short)g_agc.Erasable[0][RegCDUY],
         (unsigned short)g_agc.Erasable[0][RegCDUZ]);
  printf("  channel 030       = %06o (IMU fail bits)\n",
         (unsigned short)g_agc.InputChannel[030]);
  printf("  channel 032       = %06o\n",
         (unsigned short)g_agc.InputChannel[032]);
  printf("  channel 033       = %06o\n",
         (unsigned short)g_agc.InputChannel[033]);

  printf("\nPC bucket sample histogram (Z range -> samples):\n");
  for (int b = 0; b < 16; b++) {
    if (pc_buckets[b])
      printf("  %06o-%06o : %lu\n",
             b * 01000, (b + 1) * 01000 - 1, pc_buckets[b]);
  }

  printf("\nRaw channel 010 latches (relay rows 1..15):\n");
  for (int i = 1; i < 16; i++)
    printf("  row %2d: %06o\n", i, (unsigned short)g_dsky.latch[i]);

  printf("\nDecoded DSKY panel:\n");
  dsky_panel_t panel;
  dsky_decode_panel(&g_dsky, &panel);
  print_panel(&panel);

  /* Pass/fail heuristics: */
  int ok = 1;
  if (g_agc.CycleCounter == 0) {
    printf("\nFAIL: engine never advanced its cycle counter.\n");
    ok = 0;
  }
  if (g_dsky.generation == gen0) {
    printf("\nWARN: AGC didn't touch the DSKY in %lu cycles. May be normal if it\n"
           "      never exited standby (PIPA/CDU counters aren't wired up).\n",
           cycles);
  }

  return ok ? 0 : 1;
}
