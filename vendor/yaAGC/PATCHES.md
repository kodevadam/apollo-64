# Vendor patches

Files in this directory are copied verbatim from
https://github.com/virtualagc/virtualagc (commit pinned in
`vendor/yaAGC/UPSTREAM`) with two surgical additions to enable a libdragon
N64 build. Both changes are guarded by `-DN64` so the unmodified upstream
paths remain identical.

## `yaAGC.h`

Added a `#if defined(N64)` branch ahead of the existing `unix`/`WIN32`
branches in the OS-detection ladder. It pulls in `<stdint.h>` and defines
`FORMAT_64U` / `FORMAT_64O`. No socket headers are referenced.

## `agc_engine.h`

Added a `#elif defined (N64)` branch in the typedef ladder that just
includes `<stdint.h>`. The embedded path above it only typedefs
`int16_t`/`int8_t`/`uint16_t`, which is insufficient because `agc_t` uses
`uint32_t` (`WarningFilter`, parity table) and `uint64_t`
(`CycleCounter`, `DownruptTime`).

## Re-syncing with upstream

1. `git clone https://github.com/virtualagc/virtualagc /tmp/virtualagc`
2. `diff /tmp/virtualagc/yaAGC/yaAGC.h vendor/yaAGC/yaAGC.h`
3. `diff /tmp/virtualagc/yaAGC/agc_engine.h vendor/yaAGC/agc_engine.h`
4. Re-apply the two N64 branches above if they were lost.
5. Replace `agc_engine.c`, `NullAPI.c`, `EmbeddedDemo.c` in place (no
   patches needed in those files — they are byte-identical to upstream).
6. Update `vendor/yaAGC/UPSTREAM` with the new commit hash.

## Files NOT vendored

Upstream `yaAGC/` contains a debugger, GDB MI server, downlink decoder,
socket transport, CLI, and a ColdFire-specific embedded demo. None of
those are wired into the N64 build, so they are deliberately omitted to
keep the surface area small. If you need them, re-copy from upstream and
add them to `Makefile`.
