# Host-side tests

These run the AGC engine on your dev machine instead of on the N64, so you
can validate the pipeline without flashing a cart or firing up an emulator.

## `host_smoke`

End-to-end smoke test. Loads `src/rope.c` (the embedded Luminary099 binary)
via the same `agc_host_init()` code the N64 uses, runs the engine for a
configurable number of machine cycles, and dumps state.

```sh
make -C tests              # builds host_smoke
tests/host_smoke           # 200k cycles (default)
tests/host_smoke 5000000   # 5M cycles, ~60s of simulated AGC time
tests/host_smoke 2000000 rset   # ... and inject an RSET press halfway
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

## What this does NOT validate

- Anything libdragon-specific: `display_init`, `joypad_*`, the framebuffer
  paths in `dsky.c`, the controller mapping in `input.c`. Those need real
  N64 hardware or an emulator like ares/cen64.
- IMU/PIPA/CDU counter increments. Stubbed in `ChannelInput`.
- Real-time pacing. The smoke test runs flat-out; production needs a
  timer ISR.
- The AGC actually completing standby exit and showing a normal display.
  Requires the peripheral wiring above.
