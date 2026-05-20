# The adapter boundary

Before any spacecraft / game-world code is written, this document fixes
**where the AGC ends and the rest of the world begins**. Get this
boundary right and the physics layer can be rewritten freely without
ever risking the integrity of the guidance computer. Get it wrong and
the project stops being "a real AGC" and becomes "a thing that looks
like one".

## Three layers

```
  +-------------------------------------------------------------+
  |  outside world        spacecraft sim / game / operator      |
  |  (owns physics)       - 6-DOF state, IMU, engines, radar     |
  |                       - the DSKY operator (already built)    |
  +-------------------------------------------------------------+
                | only through I/O channels + counter pulses
  +-------------------------------------------------------------+
  |  adapter              src/agc_host.c                        |
  |  (owns translation)   - ChannelOutput / ChannelInput         |
  |                       - counter pulse generation            |
  |                       - the agc_t instance + ROM load        |
  +-------------------------------------------------------------+
                | the yaAGC callback API, nothing else
  +-------------------------------------------------------------+
  |  AGC core             vendor/yaAGC/agc_engine.c              |
  |  (owns computation)   - CPU, all memory, interrupts,         |
  |                         executive, the flight software       |
  +-------------------------------------------------------------+
```

The AGC core is **sealed**. It is unmodified Luminary099 running on the
unmodified yaAGC engine (bar the two documented `-DN64` build patches).
Nothing above it may reach in.

## The cardinal rule

> The adapter interacts with the AGC **only** through I/O channels and
> the unprogrammed-counter mechanism - exactly the wires the real
> Apollo hardware had. It never reads or writes AGC erasable or fixed
> memory to "help".

The real AGC was a closed computer: it knew the entire universe through
~20 input/output channels and a handful of hardware counters. If our
adapter respects that same surface, then what runs inside genuinely *is*
the AGC. The moment the adapter pokes a value into erasable memory to
make something work, the fidelity is gone and every later bug becomes
unfalsifiable.

Two deliberate, documented exceptions exist, both modelling real
hardware wires rather than cheating:

- `agc_host_init` seeds the cold-boot CPU state (program counter,
  `AllowInterrupt`, the channel 030-033 discrete defaults). This is the
  power-on state the hardware established, not a runtime poke.
- Channel 7 (superbank select) is mirrored into `InputChannel[7]` on
  write. That *is* the hardware behaviour - channel 7 is an output
  channel physically read back as an input. (Omitting it is what wedged
  the interpreter for most of this project's history; see the git log.)

Anything else that needs the adapter to touch `g_agc.Erasable[]` or
`g_agc.Fixed[]` directly is a design error - find the channel instead.

## Channel inventory

Direction is from the AGC's point of view. "Owner" is whoever, on the
outside, is responsible for that channel's content.

| Channel | Dir | Owner            | Meaning / adapter duty                         |
|---------|-----|------------------|------------------------------------------------|
| 01, 02  | -   | AGC core         | L, Q registers - internal, never touch.        |
| 05, 06  | out | sim (engines)    | RCS jet on/off bitmaps. Sim reads, fires jets. |
| 07      | i/o | adapter          | Superbank select. **Must echo** (see above).   |
| 010     | out | DSKY             | Display relay rows. Decoded by dsky_decode.c.  |
| 011     | out | DSKY             | Status lamps, COMP ACTY. Decoded for render.   |
| 012     | out | sim (ISS/DAP)    | IMU coarse-align, gimbal drive enables, etc.   |
| 013     | out | DSKY / sim       | DSKY test, downlink rate, RHC enables.         |
| 014     | out | sim (IMU/optics) | Gyro/CDU torque drive selects.                 |
| 015,016 | in  | DSKY operator    | Keypad. Adapter writes keycode + raises KEYRUPT.|
| 030-033 | in  | sim (IMU/ISS)    | IMU/ISS/spacecraft discrete status bits.       |
| 034,035 | out | sim (telemetry)  | Downlink word pair. Sim may stream it out.     |
| 0163    | out | DSKY             | Synthesised DSKY status (engine maintains it). |
| 0177    | -   | -                | apollo-64 sentinel for PRO; not a real channel.|

Counter registers (driven **only** via `UnprogrammedIncrement`, never by
direct assignment):

| Register        | Owner         | Adapter duty                              |
|-----------------|---------------|-------------------------------------------|
| RegPIPAX/Y/Z    | sim (IMU)     | Accelerometer pulses; rate = sensed accel.|
| RegCDUX/Y/Z     | sim (IMU)     | Gimbal-angle change pulses (PCDU/MCDU).   |
| RegRNRAD        | sim (radar)   | Radar range counter.                      |
| RegGYROCTR      | sim (IMU)     | Gyro torquing feedback.                   |
| RegTIME1..6     | AGC core      | The AGC's own timers - never touch.       |

PRO (PROCEED) is channel 032 bit 14, active-low - a discrete, handled by
`agc_host_set_pro`, not a keypad code.

## What a future spacecraft sim must implement

The sim is just "the rest of the spacecraft on the far side of the
channels". It should expose a small, polled C interface that the adapter
calls - no shared globals, no reaching across:

```c
/* Advance the simulated spacecraft by dt AGC-cycles of time. */
void  sim_step(uint32_t agc_cycles);

/* AGC outputs the sim consumes (adapter forwards from ChannelOutput). */
void  sim_set_rcs(int channel, uint16_t jets);     /* ch 05/06        */
void  sim_set_iss_discretes(int channel, uint16_t);/* ch 012/014      */

/* Sim outputs the adapter turns into AGC inputs. Called each step. */
uint16_t sim_imu_status(int channel);   /* -> InputChannel[030..033]  */
int      sim_pipa_pulse(int axis);      /* -> UnprogrammedIncrement   */
int      sim_cdu_pulse(int axis);       /* -> UnprogrammedIncrement   */
int      sim_radar_range(void);         /* -> RegRNRAD                */
```

`agc_host.c`'s `ChannelOutput` fans engine writes out to `sim_set_*`;
`ChannelInput` pulls `sim_*` results back in as counter pulses and
channel-030-033 contents. That is the *entire* coupling. The PIPA pulse
generator already in `ChannelInput` (currently a fixed 1 g stand-in) is
the prototype for `sim_pipa_pulse`.

## Timing

The sim steps in lockstep with AGC cycles, not wall-clock: `sim_step`
takes `agc_cycles` so 1 simulated second is always `AGC_PER_SECOND`
cycles on both sides, regardless of how the N64 paces real time. This
keeps guidance deterministic and replayable.

## Why this ordering matters

Physics is comparatively easy and endlessly tweakable. The boundary is
load-bearing: every guidance program (P63 descent, P40 burns, the
alignment verbs) interacts with the world *only* through the table
above. Nail the channel contract first; then the sim behind it can be a
crude point-mass today and a full 6-DOF rigid body later, and the AGC
never knows the difference - which is exactly how it was on Apollo.
