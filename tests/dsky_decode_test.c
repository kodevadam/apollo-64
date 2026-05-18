/*
 * Unit tests for src/dsky_decode.c. Hand-crafted channel 010 latch values
 * decoded against expected DSKY panel state. No yaAGC engine required.
 *
 * Run via: make -C tests run-decode-test
 *
 * If anyone refactors the relay-row decoder these tests should catch the
 * usual mistakes: swapping left/right digit positions, swapping rows,
 * confusing R1/R2/R3 sign bits, getting the 7-segment 5-bit code table
 * wrong.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/dsky_decode.h"
#include "../src/agc_host.h"

static int failures = 0;

#define EXPECT_EQ_CHAR(name, actual, expected) do { \
  if ((actual) != (expected)) { \
    printf("  FAIL: %s: got '%c' (0x%02x), expected '%c' (0x%02x)\n", \
           name, (actual), (actual), (expected), (expected)); \
    failures++; \
  } \
} while (0)

#define EXPECT_EQ_BOOL(name, actual, expected) do { \
  if ((bool)(actual) != (bool)(expected)) { \
    printf("  FAIL: %s: got %d, expected %d\n", \
           name, (int)(actual), (int)(expected)); \
    failures++; \
  } \
} while (0)

/* Helper: encode (high_5bit, low_5bit, sign_bit) into the 11-bit latch
 * payload the engine stores in OutputChannel10[row]. Row select bits
 * 14-11 are included for completeness but ignored by the decoder. */
static int16_t
latch(int row, int high, int low, int sign_bit)
{
  return (int16_t)((row << 11) | (sign_bit ? 0x0400 : 0) | ((high & 0x1F) << 5) | (low & 0x1F));
}

/* Digit-to-code (5-bit AGC display code from the yaDSKY2 table). */
static int code_for[10] = { 021, 003, 025, 027, 015, 036, 034, 023, 035, 037 };

static void
test_digit_table(void)
{
  printf("test_digit_table:\n");
  EXPECT_EQ_CHAR("blank (0)",        dsky_decode_digit(0),    ' ');
  EXPECT_EQ_CHAR("0 (021)",          dsky_decode_digit(021),  '0');
  EXPECT_EQ_CHAR("1 (003)",          dsky_decode_digit(003),  '1');
  EXPECT_EQ_CHAR("2 (025)",          dsky_decode_digit(025),  '2');
  EXPECT_EQ_CHAR("3 (027)",          dsky_decode_digit(027),  '3');
  EXPECT_EQ_CHAR("4 (015)",          dsky_decode_digit(015),  '4');
  EXPECT_EQ_CHAR("5 (036)",          dsky_decode_digit(036),  '5');
  EXPECT_EQ_CHAR("6 (034)",          dsky_decode_digit(034),  '6');
  EXPECT_EQ_CHAR("7 (023)",          dsky_decode_digit(023),  '7');
  EXPECT_EQ_CHAR("8 (035)",          dsky_decode_digit(035),  '8');
  EXPECT_EQ_CHAR("9 (037)",          dsky_decode_digit(037),  '9');
  EXPECT_EQ_CHAR("invalid (010)",    dsky_decode_digit(010),  ' ');
  /* Mask: only bottom 5 bits considered. */
  EXPECT_EQ_CHAR("masked high bits", dsky_decode_digit(0xE0 | 021), '0');
}

static void
test_prog_verb_noun(void)
{
  printf("test_prog_verb_noun:\n");
  dsky_snapshot_t snap = {0};
  /* PROG=63 (P63 lunar descent), VERB=37, NOUN=33 */
  snap.latch[11] = latch(11, code_for[6], code_for[3], 0);  /* PROG=63 */
  snap.latch[10] = latch(10, code_for[3], code_for[7], 0);  /* VERB=37 */
  snap.latch[9]  = latch(9,  code_for[3], code_for[3], 0);  /* NOUN=33 */

  dsky_panel_t p;
  dsky_decode_panel(&snap, &p);
  EXPECT_EQ_CHAR("PROG[0]", p.prog[0], '6');
  EXPECT_EQ_CHAR("PROG[1]", p.prog[1], '3');
  EXPECT_EQ_CHAR("VERB[0]", p.verb[0], '3');
  EXPECT_EQ_CHAR("VERB[1]", p.verb[1], '7');
  EXPECT_EQ_CHAR("NOUN[0]", p.noun[0], '3');
  EXPECT_EQ_CHAR("NOUN[1]", p.noun[1], '3');
}

static void
test_r1_signed(void)
{
  printf("test_r1_signed:\n");
  /* R1 = -12345 across three rows: row 8 = D1; row 7 = sign+ + D2 + D3;
   * row 6 = sign- + D4 + D5. Set sign- only -> '-'. */
  dsky_snapshot_t snap = {0};
  snap.latch[8] = latch(8, 0,           code_for[1], 0);  /* R1D1=1 */
  snap.latch[7] = latch(7, code_for[2], code_for[3], 0);  /* R1D2=2 R1D3=3 */
  snap.latch[6] = latch(6, code_for[4], code_for[5], 1);  /* R1D4=4 R1D5=5, sign-=1 */

  dsky_panel_t p;
  dsky_decode_panel(&snap, &p);
  EXPECT_EQ_CHAR("R1 sign", p.s1,    '-');
  EXPECT_EQ_CHAR("R1[0]",   p.r1[0], '1');
  EXPECT_EQ_CHAR("R1[1]",   p.r1[1], '2');
  EXPECT_EQ_CHAR("R1[2]",   p.r1[2], '3');
  EXPECT_EQ_CHAR("R1[3]",   p.r1[3], '4');
  EXPECT_EQ_CHAR("R1[4]",   p.r1[4], '5');
}

static void
test_r2_positive(void)
{
  printf("test_r2_positive:\n");
  /* R2 = +98765, sign+ from row 5, sign- absent from row 4. */
  dsky_snapshot_t snap = {0};
  snap.latch[5] = latch(5, code_for[9], code_for[8], 1);  /* sign+, R2D1=9 R2D2=8 */
  snap.latch[4] = latch(4, code_for[7], code_for[6], 0);  /* R2D3=7 R2D4=6 */
  snap.latch[3] = latch(3, code_for[5], 0,           0);  /* R2D5=5, R3D1=blank */

  dsky_panel_t p;
  dsky_decode_panel(&snap, &p);
  EXPECT_EQ_CHAR("R2 sign", p.s2,    '+');
  EXPECT_EQ_CHAR("R2[0]",   p.r2[0], '9');
  EXPECT_EQ_CHAR("R2[1]",   p.r2[1], '8');
  EXPECT_EQ_CHAR("R2[2]",   p.r2[2], '7');
  EXPECT_EQ_CHAR("R2[3]",   p.r2[3], '6');
  EXPECT_EQ_CHAR("R2[4]",   p.r2[4], '5');
}

static void
test_lamps(void)
{
  printf("test_lamps:\n");
  dsky_snapshot_t snap = {0};
  snap.channel11  = 002 | 004 | 010 | 01000;  /* COMP, UPLINK, NO_ATT, VEL */
  snap.channel163 = DSKY_KEY_REL | DSKY_RESTART | DSKY_TEMP;

  dsky_panel_t p;
  dsky_decode_panel(&snap, &p);
  EXPECT_EQ_BOOL("COMP",    p.comp_acty,   true);
  EXPECT_EQ_BOOL("UPLINK",  p.uplink_acty, true);
  EXPECT_EQ_BOOL("NO_ATT",  p.no_att,      true);
  EXPECT_EQ_BOOL("VEL",     p.vel,         true);
  EXPECT_EQ_BOOL("TRACKER", p.tracker,     false);
  EXPECT_EQ_BOOL("KEY_REL", p.key_rel,     true);
  EXPECT_EQ_BOOL("RESTART", p.restart,     true);
  EXPECT_EQ_BOOL("TEMP",    p.temp,        true);
  EXPECT_EQ_BOOL("STBY",    p.standby,     false);
}

static void
test_sign_priority(void)
{
  printf("test_sign_priority:\n");
  /* Both sign bits set -> '+' wins (matches yaDSKY2 behaviour). */
  dsky_snapshot_t snap = {0};
  snap.latch[7] = latch(7, 0, 0, 1);  /* sign+ */
  snap.latch[6] = latch(6, 0, 0, 1);  /* sign- */
  dsky_panel_t p;
  dsky_decode_panel(&snap, &p);
  EXPECT_EQ_CHAR("plus wins", p.s1, '+');
}

int
main(void)
{
  test_digit_table();
  test_prog_verb_noun();
  test_r1_signed();
  test_r2_positive();
  test_lamps();
  test_sign_priority();
  if (failures == 0) {
    printf("\nAll DSKY decode tests passed.\n");
    return 0;
  }
  printf("\n%d test failure(s).\n", failures);
  return 1;
}
