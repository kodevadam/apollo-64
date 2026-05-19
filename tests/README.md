# Host-side tests

These run the AGC engine on your dev machine instead of on the N64, so you
can validate the pipeline without flashing a cart or firing up an emulator.

## `host_smoke`

End-to-end smoke test. Loads `src/rope.c` (the embedded Luminary099 binary)
via the same `agc_host_init()` code the N64 uses, runs the engine for a
configurable number of machine cycles, and dumps state.

```sh
make -C tests                          # builds host_smoke + dsky_decode_test
tests/host_smoke                       # 200k cycles (default)
tests/host_smoke 5000000               # 5M cycles, ~60s simulated
tests/host_smoke 2000000 rset          # ...and inject RSET halfway
tests/host_smoke 2000000 alarms        # print every alarm the engine fires
tests/host_smoke 2000000 inhibit       # disable alarm-triggered GOJAMs
tests/host_smoke 2000000 noperipherals # no PIPA pulse injection
tests/host_smoke 2000000 alarms inhibit noperipherals   # combine
```

Flags are positional after the cycle count and can be combined freely.

## What's instrumented

The output dump includes:

- `CycleCounter`, PC (`Z`), `TIME1`, `TIME2` registers
- `AllowInterrupt`, `Standby`, `RestartLight` flags
- `NightWatchman`/`RuptLock`/`TCTrap`/`NoTC` watchdog state
- `WarningFilter`, `GeneratedWarning`, `ParityFail` alarm state
- `PIPAX/Y/Z` and `CDUX/Y/Z` counter registers
- `channel 030/032/033` IMU status inputs
- Raw `OutputChannel10` latch array (relay rows 1..15)
- Decoded DSKY panel (PROG/VERB/NOUN/R1/R2/R3 + lamps)
- PC bucket histogram (000000-007777 in 0512-octal-wide bins)

## `dsky_decode_test`

Standalone unit tests for the pure-C DSKY decoder. No engine link
needed, fast to run, easy to extend when adding decode logic.

```sh
make -C tests run-decode-test
```

What a healthy run looks like:

- `CycleCounter` advances at 1 per call.
- `Z` (program counter) jumps around fixed memory (003000-005000 range
  during the executive loop).
- `TIME1` increments roughly every ~858 AGC cycles (the internal scaler
  yaAGC maintains automatically).
- `DSKY generations` grows: every channel 010/011/0163 write bumps it.
- After ~1M cycles: `AllowInterrupt = 1`, `COMP ACTY` lamp lit, `RESTART`
  still on.

The `RESTART` lamp staying on is **expected**: it stays set until
software writes RSET to channel 015 from inside an interrupt handler.
Without a wired DSKY input, the AGC has no way to acknowledge. Wiring up
real interrupts (a libdragon timer ISR feeding KEYRUPT1 with the actual
key code) is what the N64 build provides, not what this host test does.

## What this validates

- yaAGC compiles and links with our `-DN64` patches.
- `agc_host_init()` correctly bank-reorders a real yaYUL `.bin` into
  `Fixed[][]` (PC ends up at 04000 as expected).
- `ChannelOutput` fires for channels 010/011/0163, mirrors the latch
  array, bumps the generation counter.
- `dsky_decode_panel()` runs without crashing on real engine state.
- The performance budget is fine: ~33 million AGC cycles/sec on a modern
  x86 means the N64 (roughly 5-10x slower per clock at this kind of code)
  will still run well above real-time.

## Trace comparison against vanilla yaAGC

`tools/trace_yaagc` connects to a running upstream `yaAGC` (via socket),
sends a configurable key sequence, and captures every channel write
yaAGC emits, in the same format as `tests/host_smoke trace=<file>`.

Workflow for diff-debugging:

```sh
# 1. Build and run upstream yaAGC on the same Luminary099 binary.
(cd /path/to/virtualagc && cmake --build build --target yaAGC)
build/yaAGC/yaAGC --quiet --nodebug --port=29991 \
  ../apollo-64/agc-software/Luminary099/Luminary099.bin &

# 2. Capture the golden trace.
tools/trace_yaagc 29991 yaagc.trace V:300 3:300 5:300 E:300

# 3. Run apollo-64 the same way and capture our trace.
tests/host_smoke 2500000 v35e trace=apollo.trace

# 4. Diff. Both formats are: <timestamp> <DIR> <chan> <val>
#    yaagc timestamps are ms-since-connect, apollo are AGC cycles.
#    1 ms ~= 85 cycles.
```

Findings from this comparison so far:

- Vanilla yaAGC running Luminary099 emits **zero channel 077 (alarm)
  writes** over 30+ seconds of execution.
- Our setup emits a NightWatchman alarm at cycle ~109k (~1.28s
  simulated) followed by repeated TC trap alarms.
- Vanilla emits VBTSTLTS lamp-test display data (channel 010 payloads
  with `0o1675` = "8 8") in response to V35E. We don't.
- Init state matches upstream `agc_engine_init.c` field-for-field.

Root cause unknown but localised: something causes Luminary's executive
not to access NEWJOB (address 0o67) within the first ~1.28s simulated,
tripping NightWatchman, which cascades into a GOJAM storm. Without
NightWatchman tripping, the executive reaches DUMMYJOB and accepts
keypresses; with it, V35E is never dispatched. Next investigation step
is to identify what Luminary is doing during cycles 0-100k that
differs between our setup and vanilla.

## Known limitation: blank DSKY without real-DSKY-handshake

Both our setup and vanilla `yaAGC` running unmodified Luminary099 produce
a blank DSKY display from cold boot, even when V35E (lamp test) or V36E
(fresh start) is injected via the canonical keystroke path
(channel 015 + KEYRUPT). This was verified by running upstream `yaAGC`
on the same `Luminary099.bin` and sending V35E over its socket protocol -
the channel 010 packet stream contains row-select bits with zero payload,
identical to what `tests/host_smoke` observes.

What we know works:
- KEYRUPT fires (`g_dsky.generation` advances, PC excursion out of
  DUMMYJOB).
- 2BLANK runs after VERB key (DSPTAB[9] gets the blank-row pattern,
  T4RUPT pushes it to channel 010 row 10).
- DSPOUT cycles through DSPTAB each T4RUPT.

What's missing: some piece of executive state (NEWJOB / WAITLIST /
specific flag) that makes the AGC's PINBALL handler actually populate
DSPTAB with digit codes when keys arrive. Real Apollo had a hardware
DSKY responding to AGC channel writes - it's possible Luminary expects
a handshake we're not providing. This needs deeper PINBALL-state
investigation, possibly with side-by-side comparison against
`yaAGC + yaDSKY2` (the real GUI), to nail down.

## What this does NOT validate

- Anything libdragon-specific: `display_init`, `joypad_*`, the framebuffer
  paths in `dsky.c`, the controller mapping in `input.c`. Those need real
  N64 hardware or an emulator like ares/cen64.
- IMU/PIPA/CDU counter increments. **PIPA pulses ARE wired** as of
  the IMU peripheral commit (~168 Hz on Z, ~4 Hz on X/Y simulating
  1g vertical). CDU pulses are still stubbed.
- Real-time pacing. The smoke test runs flat-out; production needs a
  timer ISR.
- The AGC actually completing standby exit and showing a normal display.
  Requires the peripheral wiring above.
