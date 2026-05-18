# Licensing

apollo-64 mixes three things with different licenses. Read carefully if
you intend to redistribute.

## 1. Original Apollo 11 flight software (`agc-software/`)

**Public domain.** Works of the U.S. Government are not subject to
copyright in the United States. The Apollo Guidance Computer source code
for Comanche055 and Luminary099 was produced under NASA contracts at MIT
and digitised by the Virtual AGC project and the MIT Museum.

## 2. Vendored Virtual AGC engine (`vendor/yaAGC/`)

**GPL-2.0-or-later.** Copyright (c) 2003-2025 Ronald S. Burkey and
contributors. See `vendor/yaAGC/LICENSE` for the full text and
`vendor/yaAGC/PATCHES.md` for the small diff applied to enable the N64
build.

## 3. apollo-64 N64 glue (`src/`, `tools/`, `Makefile`, docs)

**GPL-2.0-or-later.** Because the final ROM links against `agc_engine.c`
(GPL-2-or-later), the combined work is covered by the GPL. This includes:

- Source distribution of the ROM is required when binaries are distributed.
- You cannot ship a closed-source commercial cartridge.
- Forks, patches, and homebrew distribution are fine; just keep the
  source available.

## What "Public Domain Mark" applied to before this fork

The upstream `chrislgarry/Apollo-11` repository applied PDM 1.0 to the
whole repo on the basis that the only content was the Apollo source code
itself. That mark still applies to the `agc-software/` subtree here. It
does NOT apply to the engine glue we added, which is GPL by necessity
(see section 2).
