/*
 * apollo-64: host glue for yaAGC on libdragon.
 *
 * This file owns the agc_t instance and implements the five callbacks the
 * engine expects (ChannelOutput, ChannelInput, ChannelRoutine, ShiftToDeda,
 * RequestRadarData) plus the BacktraceAdd stub. It also does the
 * EmbeddedDemo.c-style ROM load.
 */

#include <string.h>

#include "agc_host.h"
#include "rope.h"

agc_t g_agc;
volatile dsky_snapshot_t g_dsky;

/* Pending keystroke. The AGC reads channel 015 bits 4:0; we hold the value
 * here and clear it after one ChannelInput poll so a held N64 button doesn't
 * register as a repeat. */
static volatile uint8_t pending_key;
static volatile bool    pending_key_dirty;

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

  /* Step 3: program counter to the boot vector. */
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
agc_host_tick(uint32_t cycles)
{
  for (uint32_t i = 0; i < cycles; i++)
    agc_engine(&g_agc);
}

/* ------------------------------------------------------------------
 * yaAGC callbacks
 * ------------------------------------------------------------------ */

void
ChannelOutput(agc_t *State, int Channel, int Value)
{
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
  }
  /* Return 0 = "no unprogrammed counter increment pending". When we wire
   * IMU CDU/PIPA counters we'll return 1 here and call
   * UnprogrammedIncrement() before returning. */
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
