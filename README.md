# apollo-64

Run the original Apollo 11 Guidance Computer flight software on a Nintendo 64.

This is a libdragon-based homebrew that boots the unmodified Luminary099 (Lunar
Module) or Comanche055 (Command Module) core rope on the N64's MIPS R4300i,
using the [Virtual AGC](https://github.com/virtualagc/virtualagc) `yaAGC`
engine as the simulator and rendering a DSKY (Display & Keyboard) to the TV.

Goal: a functional, real-time replacement for the original AGC. The CPU
fidelity comes from yaAGC; the N64 supplies a panel, a keypad, and 93 MHz of
MIPS to spend on it.

## Status

**Milestone: `flight-credible-baseline`.** apollo-64 runs unmodified
Luminary099 on a provably stock yaAGC engine, passes Luminary's own
self-check, rejects corrupted rope memory, and produces deterministic
machine-state fingerprints - all re-verified by CI on every push. See
[CHANGELOG.md](CHANGELOG.md) for the milestone note and
[docs/EQUIVALENCE.md](docs/EQUIVALENCE.md) for how the claim is kept
honest.

apollo64.z64 boots in the ares N64 emulator, the AGC executes the
flight software, and the DSKY shows real AGC state: on power-up the
ROM keys V16 N36 E and the panel displays the AGC mission clock
ticking upward; V35E lights the lamp test. The display changes
because Luminary computed it, not because the UI faked it.

- [x] yaAGC engine vendored (`vendor/yaAGC/`), pinned to upstream
- [x] Two-line patch documented under `vendor/yaAGC/PATCHES.md` to enable an
      N64 build (no socket headers, picks up `<stdint.h>`)
- [x] Host glue (`src/agc_host.c`): channel callbacks, embedded ROM loader
      with the canonical 2,3,0,1,4..35 bank reordering, DSKY snapshot bridge
- [x] DSKY decoder (`src/dsky_decode.c`): pure-C channel 010 -> PROG/VERB/
      NOUN/R1/R2/R3 + status lamps, using the same 5-bit table yaDSKY2 uses.
      Split from the renderer so it's libdragon-free and unit-testable.
- [x] DSKY renderer (`src/dsky.c`): procedurally-drawn seven-segment
      digits in electroluminescent green + backlit status-lamp tiles.
      No image-asset pipeline.
- [x] N64 controller -> DSKY keypad mapping. Pure mapping logic in
      `src/input_map.c` (libdragon-free, unit-tested); `src/input.c`
      does the joypad glue. Z is a shift modifier so all 19 DSKY keys
      are reachable.
- [x] Host tool `tools/bin2rope` to convert a yaYUL `.bin` into an embedded
      C array.
- [x] `src/rope.c` committed as a real assembled Luminary099 binary
      (regenerable via `make rope`). Builds work out of the box without
      installing yaYUL.
- [x] Host smoke test (`tests/host_smoke`) that boots the AGC on real
      Luminary099 and runs the engine. Confirms: PC at 04000, scaler ticks,
      channel writes fire, decoder doesn't crash, ~33M AGC cycles/sec on
      x86 (so N64 has ample real-time budget). See `tests/README.md`.
- [x] Real N64 ROM built end-to-end: vendored yaAGC + all `src/` files +
      libdragon compile and link with `mips64-elf-gcc` into a 224 KB
      `apollo64.z64`. ROM header is valid (z64 magic, title, region N).
      Mupen64plus accepts it - CIC type detected, video/RSP plugins
      attach, MIPS interpreter starts. Documented build path via the
      `ghcr.io/dragonminded/libdragon` docker image; see BUILDING.md.
- [x] Confirmed running in the **ares** N64 emulator: DSKY renders,
      V16N36E shows the AGC clock ticking, V35E lights the lamp test.
- [x] Luminary boots cleanly to the DUMMYJOB executive idle and runs
      indefinitely with zero alarms - matching upstream yaAGC. Three
      bugs were responsible for the long road here, all found by
      differential tracing against upstream:
        1. `AllowInterrupt` was 0 after our `memset` init (canonical
           init sets it 1) - caused a TC-trap GOJAM storm.
        2. **Channel 7 (superbank select) writes were dropped.** The
           AGC writes then reads back ch7 to address fixed-memory
           banks above 030; dropping the write corrupted the
           interpreter's bank addressing and wedged it in an infinite
           GOTO loop. One line in `ChannelOutput` fixed it.
        3. The DSKY digit-decode table had a decimal/octal mixup for
           codes 0/2/3/4 - V35E (all 8s) never exposed it; V16N36E
           did.
- [x] `tools/trace_yaagc` socket-protocol tracer + the smoke test's
      `trace=<file>` log - the diff harness that made the above
      tractable.
- [x] Key press/release modelled correctly (press raises KEYRUPT,
      release just clears channel 015 - release must NOT interrupt or
      every keystroke lights OPR ERR).
- [x] PIPA accelerometer pulse generation in `ChannelInput` (~168 Hz
      Z-axis simulating 1g vertical, 4 Hz X/Y bias).
- [ ] Real-time pacing via timer ISR (currently runs frame-batched)
- [ ] DSKY artwork (uses libdragon built-in font for now)
- [ ] CDU pulses (gimbal angle counter activity)
- [ ] Audio (1202 alarm beep, key clicks)

## Layout

```
vendor/yaAGC/        Virtual AGC engine, pinned to a known upstream commit.
                     PATCHES.md describes the two-line N64 enablement diff.
src/                 N64 application code (main loop, DSKY, input, ROM glue).
tools/               Host-side build helpers (bin2rope).
agc-software/        Original Apollo 11 flight source (.agc files), kept
                     so you can re-assemble Luminary099/Comanche055 with
                     yaYUL without re-cloning virtualagc.
```

## Quick start

```sh
# Without N64 hardware - run the AGC on your machine and see it boot:
make -C tests && tests/host_smoke 2000000

# To build the N64 ROM (see BUILDING.md for libdragon setup):
make
```

The committed `src/rope.c` already contains a real assembled Luminary099
core rope, so you don't need yaYUL to make a runnable ROM. You only need
yaYUL if you want to swap missions or modify the flight code.

## License

Mixed - read the headers.

- `vendor/yaAGC/` - GPL-2.0-or-later (Ronald S. Burkey et al.).
- `src/`, `tools/`, `Makefile` - GPL-2.0-or-later, because linking against
  yaAGC pulls the whole ROM into GPL.
- `agc-software/` (the original Apollo 11 flight source) - public domain
  (works of the U.S. Government). Transcribed by Virtual AGC + MIT Museum.

See [LICENSE.md](LICENSE.md) and `vendor/yaAGC/LICENSE` for full text.

## Credits

- Margaret Hamilton's team at MIT Instrumentation Laboratory wrote the
  original flight software in 1969.
- Paul Fjeld and Deborah Douglas digitised the printouts at the MIT Museum.
- Ronald S. Burkey and the Virtual AGC project wrote `yaAGC`, `yaYUL`, and
  the DSKY emulator this work derives from.
- Gregox273's [Embedded-AGC-DSKY-emulator](https://github.com/Gregox273/Embedded-AGC-DSKY-emulator)
  proved the engine runs unmodified on bare-metal microcontrollers; the
  approach here is the same idea on N64.
