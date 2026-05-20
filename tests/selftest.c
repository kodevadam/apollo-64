/*
 * apollo-64: AGC self-check test.
 *
 * Luminary099 ships with the Apollo "SELF-CHECK" routine
 * (AGC_BLOCK_TWO_SELF-CHECK.agc) - the same diagnostic the crew and
 * ground used to confirm the guidance computer's integrity. It runs as
 * a zero-priority background job and continuously verifies erasable
 * memory, fixed (rope) memory, and the instruction set. On a fault it
 * loads alarm code 01102 into the FAILREG triad and lights the alarm.
 *
 * This harness boots the AGC, keys the real operator sequence
 *
 *     V21 N27 E   1 0   E       (load SMODE = 010 octal: "check everything")
 *
 * lets the self-check run for tens of millions of cycles, and then
 * inspects the AGC's own erasable memory:
 *
 *   - SMODE   (01362)  must hold the keyed option, 010.
 *   - SCOUNT  (01366)  is the self-check's progress counter. It MUST
 *                      advance between two samples - that is the
 *                      positive proof the self-check is really running,
 *                      not merely "FAILREG happened to stay zero".
 *   - ERCOUNT (01365) and FAILREG (0375..0377) report faults.
 *
 * Two modes:
 *   (default)   clean rope -> expect SCOUNT advancing, no fault.
 *   "corrupt"   flip one bit of fixed memory before running -> expect
 *               the self-check to CATCH it (FAILREG = 01102). Proving
 *               it both passes good memory and rejects bad memory is
 *               what makes the clean PASS meaningful.
 *
 * Build/run: make -C tests run-selftest
 */

#include <stdio.h>
#include <string.h>

#include "../src/agc_host.h"
#include "../vendor/yaAGC/agc_engine.h"

/* Erasable addresses, resolved from the Luminary099 assembly listing. */
#define SMODE_BANK    2
#define SMODE_OFF     0362    /* SMODE   = erasable 01362        */
#define ERCOUNT_BANK  2
#define ERCOUNT_OFF   0365    /* ERCOUNT = erasable 01365        */
#define SCOUNT_BANK   2
#define SCOUNT_OFF    0366    /* SCOUNT  = erasable 01366 (3 wd) */
#define FAILREG_BANK  0
#define FAILREG_OFF   0375    /* FAILREG = erasable 0375..0377   */

#define SELFCHECK_ALARM 01102

/* Corruption target: bank 040 (octal)'s bugger word, fixed address
 * 0,3715 -> Fixed[] index [040][01715]. The bugger word is a pure
 * checksum constant, never executed as an instruction, so flipping it
 * cannot derail the CPU - it only makes that bank's sum wrong, which is
 * exactly what the self-check's fixed-memory pass is built to catch. */
#define CORRUPT_BANK 040
#define CORRUPT_OFF  01715

enum { K_0=020, K_1=001, K_2=002, K_7=007, K_VERB=021, K_NOUN=037,
       K_ENTR=034, K_RSET=022 };

int
main(int argc, char **argv)
{
  int corrupt = (argc > 1 && !strcmp(argv[1], "corrupt"));

  printf("apollo-64 AGC self-check%s\n", corrupt ? "  [corruption test]" : "");
  printf("=============================================\n");

  agc_host_init();

  if (corrupt) {
    int16_t *w = &g_agc.Fixed[CORRUPT_BANK][CORRUPT_OFF];
    int16_t before = *w;
    *w ^= 1;   /* flip one data bit of bank 040's bugger word */
    printf("  injected fault: Fixed[%o][%o]  %06o -> %06o\n",
           CORRUPT_BANK, CORRUPT_OFF,
           (unsigned short)before, (unsigned short)*w);
  }

  /* The operator key sequence, with the cycle at which to press each
   * key. Generous spacing so PINBALL fully digests every keystroke,
   * including the data-load flash/accept handshake after V21 N27 E. */
  struct { unsigned long at; uint8_t key; const char *what; } seq[] = {
    {  1500000, K_RSET, "RSET" },
    {  2000000, K_VERB, "VERB" },
    {  2300000, K_2,    "2"    },
    {  2600000, K_1,    "1"    },   /* VERB = 21 */
    {  2900000, K_NOUN, "NOUN" },
    {  3200000, K_2,    "2"    },
    {  3500000, K_7,    "7"    },   /* NOUN = 27 (SMODE) */
    {  3800000, K_ENTR, "ENTR" },   /* dispatch; AGC flashes for data */
    {  4300000, K_1,    "1"    },
    {  4600000, K_0,    "0"    },   /* SMODE option = 010 octal */
    {  4900000, K_ENTR, "ENTR" },   /* commit SMODE */
  };
  const int nseq = (int)(sizeof seq / sizeof seq[0]);

  const unsigned long batch    = 100000ul;
  const unsigned long mid      = 22000000ul;   /* first SCOUNT sample */
  const unsigned long total    = 44000000ul;   /* second SCOUNT sample */

  int16_t scount_mid[3] = {0};
  int step = 0;
  for (unsigned long done = 0; done < total; done += batch) {
    while (step < nseq && done >= seq[step].at) {
      printf("  key %-5s @ cycle %lu\n", seq[step].what, seq[step].at);
      agc_host_press_key(seq[step].key);
      step++;
    }
    agc_host_tick((uint32_t)batch);
    if (done + batch == mid)
      for (int i = 0; i < 3; i++)
        scount_mid[i] = g_agc.Erasable[SCOUNT_BANK][SCOUNT_OFF + i];
  }

  int16_t smode = g_agc.Erasable[SMODE_BANK][SMODE_OFF];
  int16_t erc   = g_agc.Erasable[ERCOUNT_BANK][ERCOUNT_OFF];
  int16_t sc[3], fr[3];
  for (int i = 0; i < 3; i++) {
    sc[i] = g_agc.Erasable[SCOUNT_BANK][SCOUNT_OFF + i];
    fr[i] = g_agc.Erasable[FAILREG_BANK][FAILREG_OFF + i];
  }
  int scount_moved = (sc[0] != scount_mid[0]) || (sc[1] != scount_mid[1])
                  || (sc[2] != scount_mid[2]);

  printf("\nAfter %lu cycles (~%lu s simulated):\n", total, total / 85333ul);
  printf("  SMODE        = %06o   (option keyed in)\n", (unsigned short)smode);
  printf("  SCOUNT @%2lus = %06o %06o %06o\n", mid / 85333ul,
         (unsigned short)scount_mid[0], (unsigned short)scount_mid[1],
         (unsigned short)scount_mid[2]);
  printf("  SCOUNT @%2lus = %06o %06o %06o   %s\n", total / 85333ul,
         (unsigned short)sc[0], (unsigned short)sc[1], (unsigned short)sc[2],
         scount_moved ? "(advanced)" : "(STALLED)");
  printf("  ERCOUNT      = %06o\n", (unsigned short)erc);
  printf("  FAILREG      = %06o %06o %06o\n",
         (unsigned short)fr[0], (unsigned short)fr[1], (unsigned short)fr[2]);

  int caught = (fr[0] == SELFCHECK_ALARM || fr[1] == SELFCHECK_ALARM
             || fr[2] == SELFCHECK_ALARM);

  if (corrupt) {
    /* On a "+" option the self-check, having caught an error, resets
     * SMODE to +0 and drops to the backup idle loop - so SMODE going
     * to 0 and SCOUNT stalling are the EXPECTED aftermath of a catch,
     * not failures. The proof is the latched alarm code + error count. */
    if (caught && erc != 0) {
      printf("\nPASS: the self-check CAUGHT the injected rope-memory fault\n"
             "      - FAILREG latched 01102 and ERCOUNT shows 1 error.\n"
             "      It is genuinely verifying memory word by word.\n");
      return 0;
    }
    printf("\nFAIL: rope memory was corrupted but the self-check did not\n"
           "      flag it (FAILREG never latched 01102 / ERCOUNT 0).\n");
    return 1;
  }

  /* Clean run: the option must have loaded, the check must be running
   * (SCOUNT advancing), and nothing may have been flagged. */
  if (smode != 010) {
    printf("\nFAIL: SMODE is not 010 - the V21 N27 load did not take, so\n"
           "      the self-check never started.\n");
    return 1;
  }
  if (!scount_moved) {
    printf("\nFAIL: SCOUNT did not advance - the self-check job is not\n"
           "      actually executing.\n");
    return 1;
  }
  if (caught) {
    printf("\nFAIL: self-check alarm 01102 latched on a CLEAN rope - the\n"
           "      diagnostic reported a fault that should not exist.\n");
    return 1;
  }
  printf("\nPASS: the Apollo self-check ran (SCOUNT advancing) and found no\n"
         "      fault - Luminary verified its own erasable memory, rope\n"
         "      memory, and instruction set on this engine. Clean.\n");
  return 0;
}
