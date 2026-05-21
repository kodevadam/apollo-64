/*
 * apollo-64: AGC multi-scenario equivalence / integration harness.
 *
 * Each scenario boots a fresh AGC, drives a scripted operator sequence,
 * and at fixed checkpoints does two independent things:
 *
 *  1. Determinism / cross-check fingerprint. A word-wise FNV hash over
 *     all of erasable memory, every I/O channel, and the cycle counter -
 *     i.e. everything that defines AGC state. The AGC is a pure integer
 *     machine: same rope + same scripted input => bit-identical state on
 *     any host. The expected fingerprints are committed below; CI asserts
 *     them, so any accidental change in AGC behaviour is caught. Because
 *     apollo-64 runs the *unmodified* yaAGC engine, the same scenario
 *     driven into a stock yaAGC build produces the same numbers (the hash
 *     is endianness-independent) - see docs/EQUIVALENCE.md.
 *
 *  2. Facet verdict. The decoded DSKY panel (src/dsky_decode.c, the same
 *     decoder the N64 renderer uses) is inspected to assert the scenario
 *     actually did what it claims - the lamp test really lit every digit,
 *     the clock really advanced, the illegal key really lit OPR ERR, and
 *     so on. A fingerprint only proves "deterministic"; the verdict
 *     proves "deterministically *correct*".
 *
 * The scenarios are chosen to cover distinct facets of the system:
 * cold-boot executive idle, the V35E lamp test (full display-relay
 * coverage), the V16N36E live clock monitor, V37 major-mode selection,
 * the V21 data-load flash/accept handshake, and the OPR ERR error path
 * plus RSET recovery. The AGC self-check has its own harness
 * (tests/selftest.c).
 *
 * Build/run:        make -C tests run-equiv
 * Regenerate golden: ./equiv regen   (paste the printed arrays back)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../src/agc_host.h"
#include "../src/dsky_decode.h"
#include "../vendor/yaAGC/agc_engine.h"

/* DSKY keypad codes (octal); see src/input.h. */
enum {
  K_0=020, K_1=001, K_2=002, K_3=003, K_4=004,
  K_5=005, K_6=006, K_7=007, K_8=010, K_9=011,
  K_VERB=021, K_RSET=022, K_KEYREL=031, K_PLUS=032,
  K_MINUS=033, K_ENTR=034, K_CLR=036, K_NOUN=037,
};

/* --- scenario model --------------------------------------------------- */

typedef struct { unsigned long cycle; uint8_t key; } event_t;

#define MAX_CHECK 4
typedef struct {
  unsigned long cycle;
  uint32_t      fp;
  dsky_panel_t  panel;
} sample_t;

typedef struct {
  const char    *name;
  const char    *facet;          /* the system facet this proves */
  const event_t *events;
  int            nevents;
  unsigned long  checkpoint[MAX_CHECK];
  int            ncheck;
  int          (*verdict)(const sample_t *s, int n, char *msg);
  uint32_t       golden[MAX_CHECK];
} scenario_t;

/* --- fingerprint ------------------------------------------------------ */

/* Word-wise FNV-1a, so the hash does not depend on host endianness. */
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

/* --- verdict helpers -------------------------------------------------- */

/* want is a 2-char NUL-terminated string; got is the char[2] panel field. */
static int
dig2(const char *got, const char *want)
{
  return got[0] == want[0] && got[1] == want[1];
}

static int
all_eq(const char *d, int n, char c)
{
  for (int i = 0; i < n; i++)
    if (d[i] != c)
      return 0;
  return 1;
}

/* --- verdict functions ------------------------------------------------ */

static int
v_coldboot(const sample_t *s, int n, char *msg)
{
  /* With no operator input the PROG and RESTART lamps stay lit - that is
   * the normal un-acknowledged fresh-start state (every scenario that
   * keys RSET clears them). What a clean boot must NOT do is raise an
   * operator error, dispatch a verb on its own, or keep churning. */
  for (int i = 0; i < n; i++) {
    if (s[i].panel.opr_err) {
      sprintf(msg, "OPR ERR latched at %lu cycles - boot was not clean",
              s[i].cycle);
      return 1;
    }
  }
  if (!s[n-1].panel.restart) {
    strcpy(msg, "RESTART lamp not lit - the AGC fresh-start never ran");
    return 1;
  }
  if (s[n-1].panel.verb[0] != ' ' || s[n-1].panel.noun[0] != ' ') {
    strcpy(msg, "a verb/noun is displayed - the AGC did not stay idle");
    return 1;
  }
  if (memcmp(&s[n-2].panel, &s[n-1].panel, sizeof(dsky_panel_t)) != 0) {
    strcpy(msg, "DSKY changed between the last two checkpoints - the "
                "boot has not settled into a stable idle");
    return 1;
  }
  strcpy(msg, "AGC ran its fresh-start (RESTART lit) and settled into a "
              "stable, quiescent executive idle - no alarms, no churn");
  return 0;
}

static int
v_lamptest(const sample_t *s, int n, char *msg)
{
  (void)n;
  const dsky_panel_t *p = &s[0].panel;
  int digits = all_eq(p->prog, 2, '8') && all_eq(p->verb, 2, '8')
            && all_eq(p->noun, 2, '8') && all_eq(p->r1, 5, '8')
            && all_eq(p->r2, 5, '8') && all_eq(p->r3, 5, '8');
  /* V35 drives the ch011 caution-lamp bank plus a few ch010 indicators. */
  int lamps  = p->uplink_acty && p->opr_err && p->prog_alarm
            && p->restart && p->key_rel && p->gimbal_lock;
  if (!digits) {
    sprintf(msg, "lamp test not lit: P=%.2s V=%.2s N=%.2s R1=%.5s",
            p->prog, p->verb, p->noun, p->r1);
    return 1;
  }
  if (!lamps) {
    strcpy(msg, "digits show 8 but the status-lamp bank is not all lit");
    return 1;
  }
  strcpy(msg, "V35E lit every seven-segment digit (8) and the status-"
              "lamp bank - the full display-relay path is exercised");
  return 0;
}

static int
v_clock(const sample_t *s, int n, char *msg)
{
  for (int i = 0; i < n; i++) {
    if (!dig2(s[i].panel.verb, "16") || !dig2(s[i].panel.noun, "36")) {
      sprintf(msg, "expected V16 N36, got V%.2s N%.2s at %lu cycles",
              s[i].panel.verb, s[i].panel.noun, s[i].cycle);
      return 1;
    }
  }
  /* N36 shows the AGC clock as hours/minutes/seconds in R1/R2/R3. Over
   * this window only minutes and seconds move, so check the whole
   * triple rather than R1 alone. */
  int advanced = memcmp(s[0].panel.r1, s[n-1].panel.r1, 5)
              || memcmp(s[0].panel.r2, s[n-1].panel.r2, 5)
              || memcmp(s[0].panel.r3, s[n-1].panel.r3, 5);
  if (!advanced) {
    strcpy(msg, "the N36 time display never changed - the AGC clock is "
                "not advancing");
    return 1;
  }
  sprintf(msg, "V16N36E monitor live: AGC clock advanced "
               "%.5s:%.5s:%.5s -> %.5s:%.5s:%.5s (Luminary computed it)",
          s[0].panel.r1, s[0].panel.r2, s[0].panel.r3,
          s[n-1].panel.r1, s[n-1].panel.r2, s[n-1].panel.r3);
  return 0;
}

static int
v_majormode(const sample_t *s, int n, char *msg)
{
  const dsky_panel_t *p = &s[n-1].panel;
  if (p->opr_err) {
    strcpy(msg, "V37 major-mode change raised OPR ERR");
    return 1;
  }
  if (!dig2(p->prog, "00")) {
    sprintf(msg, "PROG shows %.2s, expected 00 after V37 00E", p->prog);
    return 1;
  }
  strcpy(msg, "V37 00E dispatched the major-mode change - PROG shows "
              "P00, no OPR ERR");
  return 0;
}

static int
v_dataload(const sample_t *s, int n, char *msg)
{
  for (int i = 0; i < n; i++) {
    if (s[i].panel.opr_err) {
      sprintf(msg, "data load rejected - OPR ERR at %lu cycles",
              s[i].cycle);
      return 1;
    }
  }
  if (!dig2(s[n-1].panel.noun, "27")) {
    sprintf(msg, "NOUN shows %.2s after the V21 N27 load, expected 27",
            s[n-1].panel.noun);
    return 1;
  }
  if (all_eq(s[n-1].panel.r1, 5, ' ')) {
    strcpy(msg, "R1 is blank after the load - the value was never "
                "accepted or displayed back");
    return 1;
  }
  strcpy(msg, "V21 N27 E 10 E: the flash/accept data-load handshake "
              "completed, NOUN held 27, R1 shows the value, no OPR ERR");
  return 0;
}

static int
v_oprerr(const sample_t *s, int n, char *msg)
{
  (void)n;
  if (!s[0].panel.opr_err) {
    strcpy(msg, "the illegal verb did NOT light OPR ERR");
    return 1;
  }
  if (s[1].panel.opr_err) {
    strcpy(msg, "RSET did not clear OPR ERR");
    return 1;
  }
  strcpy(msg, "illegal verb lit OPR ERR; RSET cleared it - the error "
              "path and operator recovery both work");
  return 0;
}

/* --- scenarios -------------------------------------------------------- *
 * Event cycles are multiples of the 100k batch so each key lands on a
 * batch boundary. Keys are spaced ~300k cycles (~3.5 s simulated) so
 * PINBALL fully digests each keystroke, release included. */

static const event_t ev_coldboot[] = {
  { 0, 0 },   /* no operator input - pure cold-boot to idle */
};

static const event_t ev_lamptest[] = {
  { 1500000, K_RSET },
  { 2000000, K_VERB }, { 2300000, K_3 }, { 2600000, K_5 },
  { 2900000, K_ENTR },
};

static const event_t ev_clock[] = {
  { 1500000, K_RSET },
  { 2000000, K_VERB }, { 2300000, K_1 }, { 2600000, K_6 },
  { 2900000, K_NOUN }, { 3200000, K_3 }, { 3500000, K_6 },
  { 3800000, K_ENTR },
};

static const event_t ev_majormode[] = {
  { 1500000, K_RSET },
  { 2000000, K_VERB }, { 2300000, K_3 }, { 2600000, K_7 },
  { 2900000, K_ENTR },                       /* V37E - request mode change */
  { 3400000, K_0 }, { 3700000, K_0 },        /* major mode 00 */
  { 4000000, K_ENTR },
};

static const event_t ev_dataload[] = {
  { 1500000, K_RSET },
  { 2000000, K_VERB }, { 2300000, K_2 }, { 2600000, K_1 },
  { 2900000, K_NOUN }, { 3200000, K_2 }, { 3500000, K_7 },
  { 3800000, K_ENTR },                       /* V21 N27 E - AGC flashes */
  { 4300000, K_1 }, { 4600000, K_0 },        /* load value 010 */
  { 4900000, K_ENTR },
};

static const event_t ev_oprerr[] = {
  { 1500000, K_RSET },
  { 2000000, K_VERB }, { 2300000, K_0 }, { 2600000, K_1 },
  { 2900000, K_ENTR },                       /* V01E - verb needs a noun */
  { 5000000, K_RSET },                       /* operator recovery */
};

#define EV(a) a, (int)(sizeof a / sizeof a[0])

static scenario_t scenarios[] = {
  { "cold-boot", "executive boots to idle with no alarms",
    ev_coldboot, 0,                          /* nevents 0: ignore the dummy */
    { 2000000, 8000000, 16000000 }, 3,
    v_coldboot,
    { 0x721ec719, 0x2626b350, 0x8be20217 } },

  { "lamp-test", "V35E drives every display relay (digits + lamps)",
    EV(ev_lamptest),
    { 3100000 }, 1,
    v_lamptest,
    { 0x25bd3864 } },

  { "clock-monitor", "V16N36E dispatches a live monitor verb",
    EV(ev_clock),
    { 6000000, 10000000, 14000000 }, 3,
    v_clock,
    { 0xb094e7d1, 0xdb2319d1, 0x21aaeb5f } },

  { "major-mode", "V37 selects an AGC major mode (program)",
    EV(ev_majormode),
    { 7000000 }, 1,
    v_majormode,
    { 0x47cde812 } },

  { "data-load", "V21 runs the data-load flash/accept handshake",
    EV(ev_dataload),
    { 4000000, 8000000 }, 2,
    v_dataload,
    { 0x1086d7d8, 0xa8b25e86 } },

  { "opr-err", "an illegal verb lights OPR ERR; RSET recovers",
    EV(ev_oprerr),
    { 4000000, 8000000 }, 2,
    v_oprerr,
    { 0x79cf6f4b, 0xf2872433 } },
};
#define NSCEN (int)(sizeof scenarios / sizeof scenarios[0])

/* --- runner ----------------------------------------------------------- */

static void
print_panel(const dsky_panel_t *p)
{
  printf("      P=%.2s V=%.2s N=%.2s  R1=%c%.5s R2=%c%.5s R3=%c%.5s\n",
         p->prog, p->verb, p->noun,
         p->s1, p->r1, p->s2, p->r2, p->s3, p->r3);
  printf("      lamps:%s%s%s%s%s%s%s%s\n",
         p->comp_acty   ? " COMP-ACTY" : "",
         p->uplink_acty ? " UPLINK"    : "",
         p->opr_err     ? " OPR-ERR"   : "",
         p->prog_alarm  ? " PROG"      : "",
         p->restart     ? " RESTART"   : "",
         p->key_rel     ? " KEY-REL"   : "",
         p->gimbal_lock ? " GIMBAL"    : "",
         p->vn_flash    ? " (vn-flash)": "");
}

/* Run one scenario start to finish, filling samples[]. */
static void
run_scenario(const scenario_t *sc, sample_t *samples)
{
  /* Zero the samples so decoded-panel struct padding is well-defined -
   * v_coldboot memcmp's two panels. */
  memset(samples, 0, MAX_CHECK * sizeof *samples);
  agc_host_init();

  const unsigned long batch = 100000ul;
  int ev = 0, ck = 0;

  for (unsigned long done = 0; ck < sc->ncheck; done += batch) {
    while (ev < sc->nevents && done >= sc->events[ev].cycle) {
      agc_host_press_key(sc->events[ev].key);
      ev++;
    }
    agc_host_tick((uint32_t)batch);
    if (done + batch == sc->checkpoint[ck]) {
      samples[ck].cycle = sc->checkpoint[ck];
      samples[ck].fp    = fingerprint();
      dsky_decode_panel(&g_dsky, &samples[ck].panel);
      ck++;
    }
  }
}

int
main(int argc, char **argv)
{
  int regen = (argc > 1 && !strcmp(argv[1], "regen"));

  printf("apollo-64 AGC multi-scenario equivalence harness\n");
  printf("================================================\n");
  if (regen)
    printf("[regen] running scenarios, printing fingerprints to commit\n\n");

  int fp_fail = 0, verdict_fail = 0;

  for (int i = 0; i < NSCEN; i++) {
    scenario_t *sc = &scenarios[i];
    sample_t samples[MAX_CHECK];

    run_scenario(sc, samples);

    printf("\n[%d/%d] %s - %s\n", i + 1, NSCEN, sc->name, sc->facet);

    for (int c = 0; c < sc->ncheck; c++) {
      printf("  checkpoint %8lu cycles: fingerprint 0x%08x",
             samples[c].cycle, samples[c].fp);
      if (regen) {
        printf("\n");
      } else if (samples[c].fp == sc->golden[c]) {
        printf("  OK\n");
      } else {
        printf("  MISMATCH (expected 0x%08x)\n", sc->golden[c]);
        fp_fail++;
      }
      print_panel(&samples[c].panel);
    }

    char msg[200];
    int bad = sc->verdict(samples, sc->ncheck, msg);
    if (regen) {
      printf("  verdict: %s\n", msg);
    } else if (bad) {
      printf("  VERDICT FAIL: %s\n", msg);
      verdict_fail++;
    } else {
      printf("  verdict OK: %s\n", msg);
    }
  }

  if (regen) {
    printf("\nGolden fingerprints - paste each line into its scenario:\n");
    for (int i = 0; i < NSCEN; i++) {
      sample_t samples[MAX_CHECK];
      run_scenario(&scenarios[i], samples);
      printf("  %-13s { ", scenarios[i].name);
      for (int c = 0; c < scenarios[i].ncheck; c++)
        printf("0x%08x, ", samples[c].fp);
      printf("}\n");
    }
    return 0;
  }

  printf("\n================================================\n");
  if (fp_fail == 0 && verdict_fail == 0) {
    printf("PASS: all %d scenarios - every checkpoint deterministic and\n"
           "      every facet verified correct.\n", NSCEN);
    return 0;
  }
  printf("FAIL: %d fingerprint mismatch(es), %d verdict failure(s).\n",
         fp_fail, verdict_fail);
  return 1;
}
