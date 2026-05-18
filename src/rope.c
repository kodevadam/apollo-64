/*
 * Placeholder core rope. All zeros - the AGC will boot, fault, and sit in
 * an alarm loop. Replace with output of `tools/bin2rope <mission>.bin` to
 * run real flight software (Luminary099 for the LM, Comanche055 for the
 * CM).
 *
 * Reason this file exists at all: it lets the project build out-of-the-box
 * without yaYUL installed, which makes iterating on the libdragon side
 * faster. CI / a real flash needs the regenerated version.
 */

#include "rope.h"

const unsigned char CoreRope[CORE_SIZE][2] = { { 0, 0 } };
