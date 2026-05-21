# apollo-64

Run the original Apollo 11 Guidance Computer flight software on a Nintendo 64.

apollo-64 is a libdragon homebrew that boots the unmodified Luminary099 (Lunar
Module) core rope on the N64's MIPS R4300i, using the
[Virtual AGC](https://github.com/virtualagc/virtualagc) `yaAGC` engine as the
CPU and rendering a DSKY (Display & Keyboard) to the TV. The CPU fidelity comes
from yaAGC; the N64 supplies a panel, a keypad, and the MIPS to run it in real
time.

## Status

**Milestone: `flight-credible-baseline`.** apollo-64 runs unmodified
Luminary099 on a provably stock yaAGC engine, passes Luminary's own self-check,
rejects corrupted rope memory, and produces deterministic machine-state
fingerprints - all re-verified by CI on every push.

The ROM (`apollo64.z64`) boots in the ares N64 emulator: the AGC executes the
flight software and the DSKY shows real AGC state. On power-up it keys V16 N36 E
and the panel displays the mission clock ticking upward; V35E lights the lamp
test. The display changes because Luminary computed it, not because the UI
faked it.

See [CHANGELOG.md](CHANGELOG.md) for the milestone note and
[docs/EQUIVALENCE.md](docs/EQUIVALENCE.md) for how the "genuine AGC" claim is
kept honest. Not yet done: real-time pacing via timer ISR, DSKY artwork, CDU
pulses, and audio.

## Layout

```
vendor/yaAGC/        Virtual AGC engine, pinned to a known upstream commit.
src/                 N64 application code (main loop, DSKY, input, ROM glue).
tools/               Host-side build helpers (bin2rope).
agc-software/        Original Apollo 11 flight source (.agc files), kept so you
                     can re-assemble Luminary099/Comanche055 with yaYUL.
```

## Quick start

```sh
# Without N64 hardware - run the AGC on your machine and see it boot:
make -C tests && tests/host_smoke 2000000

# To build the N64 ROM (see BUILDING.md for libdragon setup):
make
```

The committed `src/rope.c` already contains a real assembled Luminary099 core
rope, so you don't need yaYUL to build a runnable ROM. You only need yaYUL to
swap missions or modify the flight code.

## License

Mixed - read the headers.

- `vendor/yaAGC/` - GPL-2.0-or-later (Ronald S. Burkey et al.).
- `src/`, `tools/`, `Makefile` - GPL-2.0-or-later, because linking against
  yaAGC pulls the whole ROM into GPL.
- `agc-software/` (the original Apollo 11 flight source) - public domain
  (works of the U.S. Government). Transcribed by Virtual AGC + MIT Museum.

See [LICENSE.md](LICENSE.md) and `vendor/yaAGC/LICENSE` for full text.

## Credits

- Margaret Hamilton's team at MIT Instrumentation Laboratory wrote the original
  flight software in 1969.
- Paul Fjeld and Deborah Douglas digitised the printouts at the MIT Museum.
- Ronald S. Burkey and the Virtual AGC project wrote `yaAGC`, `yaYUL`, and the
  DSKY emulator this work derives from.
- Gregox273's [Embedded-AGC-DSKY-emulator](https://github.com/Gregox273/Embedded-AGC-DSKY-emulator)
  proved the engine runs unmodified on bare-metal microcontrollers; the
  approach here is the same idea on N64.
