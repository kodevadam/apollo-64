# Equivalence with upstream yaAGC

The claim apollo-64 makes is strong: it runs the *genuine* Apollo
Guidance Computer, not an approximation. This document sets out how
that claim is kept honest and machine-checked.

The argument has four legs, each backed by an automated test in CI.

## 1. The engine is upstream's exact code

`vendor/yaAGC/agc_engine.c` - the CPU, the executive, the instruction
set, the interrupt and counter machinery - is **byte-identical** to the
Virtual AGC project's `agc_engine.c` at the commit pinned in
`vendor/yaAGC/UPSTREAM`. `NullAPI.c` and `EmbeddedDemo.c` likewise.

The only vendored files that differ are two headers, `agc_engine.h` and
`yaAGC.h`, and they differ by *exactly* the `-DN64` build branches
recorded in `vendor/yaAGC/PATCHES.md` - header includes and integer
typedefs, no logic. The expected diffs are committed
(`*.expected-diff`).

`tools/verify_vendor.sh` fetches the pinned upstream commit and checks
all of this: byte-identity for the `.c` files, exact-patch-match for the
headers. It runs in CI. If anyone alters the engine, CI fails.

So the AGC inside apollo-64 *is* upstream yaAGC. There is nothing to
"port" at the engine level - it is the same machine.

## 2. The flight software is unmodified

`agc-software/Luminary099/` is the Virtual AGC transcription of the
Apollo 11 Lunar Module flight rope. `src/rope.c` is the assembled image
(`tools/bin2rope` of the yaYUL output). It is the real Luminary099,
loaded into the real fixed-memory map with the canonical bank order.

## 3. The computer verifies itself

`tests/selftest.c` keys the genuine operator sequence `V21 N27 E 10 E`
to run Luminary's built-in **SELF-CHECK** diagnostic - the same routine
the crew and ground used. It checks erasable memory, rope memory, and
the instruction set. apollo-64 runs it both ways:

- clean rope: the self-check runs (its `SCOUNT` progress counter
  advances) and reports no fault;
- one rope word deliberately corrupted: the self-check **catches it** -
  alarm code `01102` latches in `FAILREG`, `ERCOUNT` increments.

Passing good memory *and* rejecting bad memory is what makes the clean
pass meaningful. The computer is genuinely testing itself.

## 4. Behaviour is deterministic, and every facet is exercised

The AGC is a pure integer machine: given the same rope and the same
scripted input it must reach bit-identical state every run, on any
host. `tests/equiv.c` is a multi-scenario harness - it drives six
scripted operator sessions, each isolating a distinct facet of the
system:

| Scenario      | Facet it proves |
|---------------|-----------------|
| cold-boot     | the executive boots, with no operator input, to a stable idle |
| lamp-test     | V35E drives every seven-segment digit and the status-lamp bank |
| clock-monitor | V16N36E dispatches a live monitor verb; the AGC clock advances |
| major-mode    | V37 selects an AGC major mode (program) |
| data-load     | V21's flash/accept data-load handshake completes |
| opr-err       | an illegal verb lights OPR ERR, and RSET clears it |

Each scenario boots a fresh AGC and, at fixed checkpoints, does two
independent checks:

- **Fingerprint.** A word-wise hash of the *entire* machine state - all
  erasable memory, every I/O channel, the cycle counter. The expected
  values are committed; CI asserts them, so any accidental change in AGC
  behaviour is caught. The hash is endianness-independent, so the same
  scenario driven into a stock yaAGC build produces the same numbers -
  the reference side of an upstream cross-check.
- **Verdict.** The decoded DSKY panel (the same `dsky_decode.c` the N64
  renderer uses) is inspected to assert the facet actually happened -
  the lamp test really lit every digit, the clock really advanced, the
  illegal key really lit OPR ERR. A fingerprint proves "deterministic";
  the verdict proves "deterministically *correct*".

## What this adds up to

| Leg | Question it answers | Check |
|-----|--------------------|-------|
| 1   | Is the engine the real AGC?      | `verify_vendor.sh` |
| 2   | Is the flight software real?     | `rope.c` from `agc-software/` |
| 3   | Does the computer work?          | `selftest` (clean + fault) |
| 4   | Is behaviour stable & correct?   | `equiv` (6 scenarios) |

Engine identical to upstream, flight software unmodified, the machine
passing its own self-check, behaviour deterministic and pinned - that
is the substance behind "apollo-64 runs a genuine AGC", and every line
of it is re-verified on every push.
