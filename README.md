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

Scaffolded, not yet running on hardware.

- [x] yaAGC engine vendored (`vendor/yaAGC/`)
- [x] Two-line patch documented under `vendor/yaAGC/PATCHES.md` to enable an
      N64 build (no socket headers, picks up `<stdint.h>`)
- [x] Host glue (`src/agc_host.c`): channel callbacks, embedded ROM loader
      with the canonical 2,3,0,1,4..35 bank reordering, DSKY snapshot bridge
- [x] DSKY renderer (`src/dsky.c`): decodes channel 010 relay rows into
      PROG/VERB/NOUN + R1/R2/R3 + status lamps using the same 5-bit table
      yaDSKY2 uses
- [x] N64 controller -> DSKY keypad mapping (`src/input.c`)
- [x] Host tool `tools/bin2rope` to convert a yaYUL `.bin` into an embedded C
      array
- [ ] Actually built and tested on hardware/emulator. The scaffold has not
      been compiled in this repo - libdragon and the N64 toolchain weren't
      installed at scaffold time. See BUILDING.md.
- [ ] Real-time pacing via timer ISR (currently runs frame-batched)
- [ ] DSKY artwork (uses libdragon built-in font for now)
- [ ] IMU/PIPA/CDU counter wiring
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

See [BUILDING.md](BUILDING.md). Roughly:

```sh
# 1. Build libdragon, set $N64_INST. (See libdragon README.)
# 2. Assemble a mission once on the host (needs yaYUL from virtualagc):
yaYUL agc-software/Luminary099/MAIN.agc
# 3. Embed the output in the ROM:
make rope MISSION=Luminary099
# 4. Build the N64 ROM:
make
# 5. Flash apollo64.z64 to a flashcart, or run in cen64/ares/Mupen64Plus.
```

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
