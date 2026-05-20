/*
 * apollo-64: host glue for yaAGC on libdragon.
 *
 * This file owns the agc_t instance and implements the five callbacks the
 * engine expects (ChannelOutput, ChannelInput, ChannelRoutine, ShiftToDeda,
 * RequestRadarData) plus the BacktraceAdd stub. It also does the
 * EmbeddedDemo.c-style ROM load.
 */

#include <stdio.h>
#include <string.h>

#include "agc_host.h"
#include "rope.h"

agc_t g_agc;
volatile dsky_snapshot_t g_dsky;

/* Optional trace logger. When non-NULL, every relevant channel I/O event
 * is appended in the same format as tools/trace_yaagc, so we can diff
 * apollo-64's behaviour against vanilla yaAGC+yaDSKY2 on identical ROMs. */
FILE *g_agc_trace;

/* Pending keystroke. The AGC reads channel 015 bits 4:0; we hold the value
 * here and clear it after one ChannelInput poll so a held N64 button doesn't
 * register as a repeat.
 *
 * Real DSKY hardware drives ch15 to the key code while held, then back to 0
 * on release. yaDSKY2 sends both events as separate ch15 packets, and
 * Luminary's PINBALL handler relies on seeing the release - without it the
 * VERB+digits+ENTER sequence accumulates "stuck" key state and verbs like
 * V35E never dispatch properly. We model the same: emit press, then a few
 * cycles later emit release. */
#define KEY_RELEASE_DELAY 25500 /* ~300ms simulated hold time, matches the
                                 * timing yaDSKY2 produces with a human
                                 * pressing buttons. 200 cycles (2.4ms) was
                                 * too short - PINBALL's debounce / job
                                 * scheduling apparently relies on the AGC
                                 * actually having time to read the key
                                 * before the release fires. */
static volatile uint8_t  pending_key;
static volatile bool     pending_key_dirty;
static volatile uint16_t key_release_counter;  /* 0 = idle, otherwise countdown */

/* PIPA counter pacing. The AGC's IMU subsystem needs to see PIPA pulses
 * to declare the IMU alive; without them Luminary won't exit its restart
 * loop. We simulate "spacecraft sitting on the pad" by generating PIPAZ
 * pulses at ~168 Hz (the rate corresponding to 1g vertical acceleration,
 * given 5.85 cm/s per PIPA pulse). PIPAX and PIPAY get tiny rates to
 * keep the IMU monitor happy but not bias the navigation. */
#define AGC_CYCLES_PER_SEC ((1024000UL + 6) / 12)         /* ~85333 */
#define PIPA_Z_PERIOD      (AGC_CYCLES_PER_SEC / 168)     /* ~510 cycles */
#define PIPA_XY_PERIOD     (AGC_CYCLES_PER_SEC / 4)       /* ~21333 cycles, 4 Hz */
static uint32_t pipa_z_counter;
static uint32_t pipa_x_counter;
static uint32_t pipa_y_counter;
static bool     peripherals_enabled = true;


extern void UnprogrammedIncrement(agc_t *State, int Counter, int IncType);

void
agc_host_init(void)
{
  memset(&g_agc, 0, sizeof g_agc);

  /* Step 1 (per EmbeddedDemo.c): copy CoreRope[] into State.Fixed[][] with
   * bank reordering 2,3,0,1,4,5,...,35. CoreRope is laid out as the yaYUL
   * .bin file: big-endian 16-bit words, parity in the LSB. */
  int bank = 2, j = 0;
  for (int i = 0; i < AGC_CORE_ROPE_WORDS; i++) {
    uint16_t raw = ((uint16_t)CoreRope[i][0] << 8) | CoreRope[i][1];
    g_agc.Fixed[bank][j++] = (int16_t)(raw >> 1);
    if (j == AGC_BANK_WORDS) {
      j = 0;
      switch (bank) {
        case 2:  bank = 3; break;
        case 3:  bank = 0; break;
        case 0:  bank = 1; break;
        case 1:  bank = 4; break;
        default: bank++;   break;
      }
    }
  }

  /* Step 2: I/O channel defaults that the AGC expects on cold boot. */
  g_agc.InputChannel[030] = 037777;
  g_agc.InputChannel[031] = 077777;
  g_agc.InputChannel[032] = 077777;
  g_agc.InputChannel[033] = 077777;

  /* Step 3: CPU state that needs explicit setup (memset zeros aren't
   * always the right values). The canonical agc_engine_init.c does
   * these and we were missing them - without AllowInterrupt=1 the AGC's
   * boot sequence sits in a TC loop, gets TCTrap'd every 5 ms, and
   * GOJAMs back to 04000 forever (queued T3/T4 interrupts can't be
   * dispatched until the AGC software performs RELINT, which it never
   * reaches). */
  g_agc.AllowInterrupt = 1;
  g_agc.DownruptTimeValid = 1;
  g_agc.DownruptTime = 0;
  /* Note: vanilla agc_engine_init.c sets InterruptRequests[8]=1 then
   * immediately wipes it to 0 in a clear-all loop. Net effect: 0. We
   * match that. The earlier comment about it being "the first ISR
   * kick" was wrong - the comment refers to a different code path
   * (core-dump load) that doesn't apply at cold boot. */

  /* Step 4: program counter to the boot vector. */
  g_agc.Erasable[0][RegZ] = 04000;


  g_dsky.generation = 1;
}

void
agc_host_press_key(uint8_t key_code)
{
  pending_key = key_code & 0x1F;
  pending_key_dirty = true;
}

void
agc_host_set_pro(bool held)
{
  /* PRO is channel 032 bit 14, active-low. The engine polls this on every
   * scaler tick to set State->SbyPressed, which in turn drives standby
   * entry/exit after the required hold time. */
  if (held)
    g_agc.InputChannel[032] &= (int16_t)~020000;
  else
    g_agc.InputChannel[032] |= (int16_t)020000;
}

void
agc_host_tick(uint32_t cycles)
{
  for (uint32_t i = 0; i < cycles; i++)
    agc_engine(&g_agc);
}

void
agc_host_set_peripherals(bool on)
{
  peripherals_enabled = on;
}

/* ------------------------------------------------------------------
 * yaAGC callbacks
 * ------------------------------------------------------------------ */

void
ChannelOutput(agc_t *State, int Channel, int Value)
{
  if (g_agc_trace)
    fprintf(g_agc_trace, "%6lu OUT %03o %06o\n",
            (unsigned long)State->CycleCounter, Channel, Value & 077777);

  /* Channel 7 is the superbank-select. It is an OUTPUT channel whose value
   * the AGC also needs to READ BACK (it is overlapped with input channel 7).
   * The CPU uses it to address fixed-memory banks above 030. If a write to
   * channel 7 is dropped, the AGC's next superbank-relative fetch reads the
   * wrong bank - which corrupts the interpreter's instruction stream and
   * sends it into an infinite GOTO-indirection loop. yaAGC's SocketAPI
   * mirrors the write into InputChannel[7]; NullAPI (and our old code)
   * silently dropped it. This single line is the difference between
   * Luminary running and Luminary wedging within the first half second. */
  if (Channel == 7) {
    State->InputChannel[7] = State->OutputChannel7 = (Value & 0160);
    return;
  }

  /* Channel 010 is the DSKY display latch. The engine has already stored the
   * decoded relay row in State->OutputChannel10[row]; we just mirror the
   * array and bump the generation counter so the renderer redraws. */
  if (Channel == 010) {
    for (int i = 0; i < 16; i++)
      g_dsky.latch[i] = State->OutputChannel10[i];
    g_dsky.channel163 = State->DskyChannel163;
    g_dsky.generation++;
    return;
  }
  if (Channel == 011) { g_dsky.channel11 = (int16_t)Value; g_dsky.generation++; return; }
  if (Channel == 013) { g_dsky.channel13 = (int16_t)Value; g_dsky.generation++; return; }
  if (Channel == 0163) { g_dsky.channel163 = (int16_t)Value; g_dsky.generation++; return; }
  /* Other output channels (engine-on, RCS firing, telemetry, IMU torquing
   * commands, etc.) are accepted silently for now. */
}

int
ChannelInput(agc_t *State)
{
  if (pending_key_dirty) {
    pending_key_dirty = false;
    State->InputChannel[015] = pending_key;
    State->InterruptRequests[5] = 1;  /* KEYRUPT1 */
    key_release_counter = KEY_RELEASE_DELAY;
    if (g_agc_trace)
      fprintf(g_agc_trace, "%6lu IN  015 %06o   # press\n",
              (unsigned long)State->CycleCounter, pending_key);
  } else if (key_release_counter) {
    if (--key_release_counter == 0) {
      /* Synthetic key release: ch15 -> 0 with KEYRUPT, exactly what
       * yaDSKY2 sends on real button-up. PINBALL's CHARIN handler
       * sees code 0, falls through to CHARALRM which is a no-op for
       * already-cleared error state - the important effect is that
       * PINBALL's internal "last key" debounce is reset, so the next
       * digit doesn't get folded into the previous one. */
      State->InputChannel[015] = 0;
      State->InterruptRequests[5] = 1;
      if (g_agc_trace)
        fprintf(g_agc_trace, "%6lu IN  015 000000   # release\n",
                (unsigned long)State->CycleCounter);
    }
  }

  /* Drive PIPA counters. The engine guarantees one instruction between
   * ChannelInput calls, so each pulse here corresponds to one machine
   * cycle of "skipped" execution (which is fine - real PIPA pulses also
   * consume a cycle for the unprogrammed-sequence handler). At most one
   * PIPA pulse per ChannelInput call. */
  if (peripherals_enabled) {
    if (++pipa_z_counter >= PIPA_Z_PERIOD) {
      pipa_z_counter = 0;
      UnprogrammedIncrement(State, RegPIPAZ, 0);  /* PINC */
      return 1;
    }
    if (++pipa_x_counter >= PIPA_XY_PERIOD) {
      pipa_x_counter = 0;
      UnprogrammedIncrement(State, RegPIPAX, 0);
      return 1;
    }
    if (++pipa_y_counter >= PIPA_XY_PERIOD) {
      pipa_y_counter = 0;
      UnprogrammedIncrement(State, RegPIPAY, 0);
      return 1;
    }
  }
  return 0;
}

void
ChannelRoutine(agc_t *State)
{
  (void)State;
  /* Hook point for periodic housekeeping. The socket build flushes batched
   * I/O here; we have nothing to do. */
}

void
ShiftToDeda(agc_t *State, int Data)
{
  (void)State; (void)Data;  /* AGS only - the LM had a separate abort computer. */
}

void
RequestRadarData(agc_t *State)
{
  (void)State;
  /* Stub. When wired up: State->Erasable[0][RegRNRAD] = <ranging counter>; */
}

void
BacktraceAdd(agc_t *State, int Cause)
{
  (void)State; (void)Cause;
  /* Backtrace is a debugger feature - dropping it makes the engine measurably
   * faster, which matters when running real-time on a 93 MHz N64. */
}
