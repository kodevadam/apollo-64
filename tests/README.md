# Host-side tests

These run the AGC engine on your dev machine instead of on the N64, so you
can validate the pipeline without flashing a cart or firing up an emulator.

## `host_smoke`

End-to-end smoke test. Loads `src/rope.c` (the embedded Luminary099 binary)
via the same `agc_host_init()` code the N64 uses, runs the engine for a
configurable number of machine cycles, and dumps state.

```sh
make -C tests                          # builds all host tests
tests/host_smoke                       # 200k cycles (default)
tests/host_smoke 5000000               # 5M cycles, ~60s simulated
tests/host_smoke 2000000 rset          # inject RSET early (clear restart)
tests/host_smoke 2500000 rset v35e     # ...then V35E lamp test - panel
                                       #   shows 88 / +88888 across the board
tests/host_smoke 4000000 rset v16n36e  # ...then V16 N36 E - panel shows the
                                       #   AGC clock; R3 ticks between runs
tests/host_smoke 2000000 alarms        # print every alarm the engine fires
tests/host_smoke 2000000 inhibit       # disable alarm-triggered GOJAMs
tests/host_smoke 2000000 noperipherals # no PIPA pulse injection
tests/host_smoke 2000000 trace=out.log # log channel I/O for diffing
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

## `selftest` and `equiv`

`selftest` runs Luminary's own SELF-CHECK diagnostic (`V21 N27 E 10 E`)
and confirms it both passes a clean rope and catches a corrupted one.

`equiv` is the multi-scenario equivalence harness: six scripted operator
sessions (cold boot, V35E lamp test, V16N36E clock monitor, V37 major-
mode select, V21 data load, OPR ERR + RSET recovery). Each session is
fingerprinted at fixed checkpoints for determinism, and verdict-checked
against the decoded DSKY panel for correctness. `./equiv regen` reprints
the fingerprints when an intentional behaviour change needs new goldens.
See `docs/EQUIVALENCE.md`.

```sh
make -C tests run-selftest
make -C tests run-equiv
```

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

This comparison harness cracked the DSKY-blank problem. The chain of
findings:

1. Vanilla yaAGC ran Luminary099 with zero alarms; our setup tripped
   NightWatchman at cycle ~109k and never displayed anything.
2. A minimal harness (vanilla `agc_engine_init` + `NullAPI` + a
   flat-out engine loop) reproduced the bug - so it was not our init.
3. Swapping `NullAPI` for `SocketAPI` in that harness fixed it.
4. Diffing the two: `SocketAPI::ChannelOutput` has a channel-7
   (superbank select) case that `NullAPI` - and our `agc_host.c` -
   lacked. A dropped channel-7 write corrupts fixed-memory bank
   addressing and wedges the interpreter in an infinite GOTO loop.

The fix was one case in `ChannelOutput` (see the commit "Handle
channel 7"). With it, our NEWJOB-access gap dropped from 74619 cycles
to 2312 - matching vanilla's 2299 - and the AGC runs indefinitely
with zero alarms.

V35E (lamp test) and V16N36E (AGC clock monitor) both now work end to
end. The trace harness remains useful for any future divergence
debugging.

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
