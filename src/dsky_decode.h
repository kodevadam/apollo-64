#ifndef APOLLO64_DSKY_DECODE_H
#define APOLLO64_DSKY_DECODE_H

/*
 * Pure-C DSKY state decoder. Separate from dsky.c so it can be unit-tested
 * on the host without a libdragon dependency.
 *
 * Source of truth for the channel 010 relay-row layout is yaDSKY2 (see
 * vendor/yaAGC reference). 5-bit code -> ASCII digit table is identical to
 * the SevenSegmentFilenames[] array in yaDSKY2.cpp.
 */

#include <stdbool.h>
#include <stdint.h>

#include "agc_host.h"  /* for dsky_snapshot_t */

typedef struct {
  char prog[2];     /* PROG digits */
  char verb[2];     /* VERB digits */
  char noun[2];     /* NOUN digits */
  char r1[5];       /* R1 digits (no sign char) */
  char r2[5];
  char r3[5];
  char s1;          /* '+', '-', or ' ' */
  char s2;
  char s3;
  bool comp_acty;
  bool uplink_acty;
  bool no_att;
  bool standby;
  bool key_rel;
  bool opr_err;
  bool prio_disp;
  bool temp;
  bool gimbal_lock;
  bool prog_alarm;
  bool restart;
  bool tracker;
  bool alt;
  bool vel;
} dsky_panel_t;

/* Decode an AGC display 5-bit code into ASCII ('0'..'9' or ' '). */
char dsky_decode_digit(uint8_t code);

/* Project the live channel state into a decoded panel snapshot. */
void dsky_decode_panel(const volatile dsky_snapshot_t *src, dsky_panel_t *dst);

#endif
