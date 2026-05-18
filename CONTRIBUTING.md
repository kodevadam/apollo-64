# Contributing

Two scopes of contribution, with different rules:

## 1. The N64 port (`src/`, `tools/`, `Makefile`, docs)

Normal homebrew PR flow. Things that are particularly welcome:

- **IMU/PIPA/CDU counter wiring** in `ChannelInput` so guidance programs
  actually receive sensor data.
- **Real-time pacing** via a libdragon timer ISR instead of frame batching.
- **DSKY artwork** - convert the JPGs from upstream `yaDSKY2/` into a
  proper sprite sheet, replace the placeholder text rendering.
- **Audio** - the 1202 alarm tone is one square wave.
- **Real-hardware testing** on EverDrive 64 / 64drive / SummerCart64.

Code style: stick to plain C99, no exotic libdragon features unless they
buy something specific. Keep `#include` lists short and avoid pulling
extra stuff into `agc_host.c` - that file's whole reason for existing is
to be the only thing that touches both the engine and libdragon.

## 2. The vendored engine (`vendor/yaAGC/`)

**Don't.** Patches there should go upstream to
https://github.com/virtualagc/virtualagc first. The only modifications
this repo carries are documented in `vendor/yaAGC/PATCHES.md` and exist
solely to make the engine compile for N64; anything else belongs
upstream.

To re-sync after an upstream change:

1. Update the commit pinned in `vendor/yaAGC/UPSTREAM`.
2. Re-apply the patches per `PATCHES.md`.
3. Spot-check the diff on `agc_engine.c` for new function signatures that
   might affect our callbacks (`ChannelOutput`, `ChannelInput`,
   `ChannelRoutine`, `BacktraceAdd`, `ShiftToDeda`, `RequestRadarData`).

## 3. The Apollo flight source (`agc-software/`)

This is a verbatim copy of the transcription from the Virtual AGC
project. Typos and discrepancies should be reported upstream at
https://github.com/virtualagc/virtualagc - we just mirror it here so the
build doesn't need a second clone.
