# Changelog

apollo-64 - the Apollo 11 Guidance Computer, running on a Nintendo 64.

## flight-credible-baseline (2026-05-20)

The validation milestone - the point at which apollo-64 stops being a
demo and becomes something with a real, machine-checked claim behind it.

**The claim:** apollo-64 runs unmodified Luminary099 on a provably
stock yaAGC engine, passes Luminary's own self-check, rejects corrupted
rope memory, and produces deterministic machine-state fingerprints -
all re-verified by CI on every push.

### What the claim rests on

Four legs, each backed by an automated test (see `docs/EQUIVALENCE.md`):

1. **Stock engine.** `vendor/yaAGC/agc_engine.c` - the CPU, executive,
   instruction set - is byte-identical to the Virtual AGC project at the
   pinned commit. The two headers differ only by the documented `-DN64`
   build branch. `tools/verify_vendor.sh` proves it in CI.
2. **Unmodified flight software.** `src/rope.c` is the assembled
   Luminary099 image; `agc-software/Luminary099/` is the verbatim
   transcription.
3. **The computer self-checks.** `tests/selftest` keys the real
   `V21 N27 E 10 E` sequence to run Luminary's SELF-CHECK diagnostic.
   Clean rope: it runs (SCOUNT advances) and reports no fault. One rope
   word corrupted: it catches the fault (FAILREG = 01102).
4. **Deterministic behaviour.** `tests/equiv` runs a scripted operator
   scenario and fingerprints the entire machine state at five
   checkpoints; the golden fingerprints are committed and CI-asserted.

### Capabilities at this baseline

- Real N64 ROM (`apollo64.z64`), builds via libdragon; boots and runs
  in the ares emulator.
- DSKY renderer: procedural seven-segment digits, backlit lamp panel
  (lamp wiring verified against yaDSKY2), Verb/Noun flash.
- Boots to a live AGC mission clock (auto V16 N36 E); V35E lamp test.
- Real-time pacing (AGC time tracks the wall clock, frame-drop immune).
- Procedural audio: DSKY key click + AGC caution tone on OPR ERR.
- N64 controller -> DSKY keypad, Z-shift layer, all 19 keys reachable;
  mapping and glue host-unit-tested.
- CI: nine host checks (decode, input-map, input-poll, smoke,
  self-check x2, equivalence, vendor-verify) plus the full N64 ROM
  build.

### Deliberately out of scope at this baseline

Deferred to later phases - not mixed into this milestone:

- Spacecraft / world simulation (Tier 2). The adapter boundary is
  designed in `docs/ADAPTER.md`; no sim code yet.
- Expanded equivalence scenarios (P-program selection, data-load
  verbs, OPR ERR paths, restart/recovery).
- Literal runtime cross-check against a separately-built stock yaAGC
  (the byte-identical vendor proof already covers this; this would be
  belt-and-suspenders).
- Live-controller verification inside an emulator, and a real-hardware
  run from a flashcart.
