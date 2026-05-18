# Building apollo-64

You need three things:

1. A working **libdragon** install (N64 SDK).
2. The **yaYUL** assembler from Virtual AGC, to turn `.agc` source files
   into the binary that gets embedded in the ROM.
3. Your **host C compiler** to build `tools/bin2rope`.

apollo-64 itself doesn't need anything else - the yaAGC engine is vendored
in tree.

## 1. libdragon

Follow the upstream README at https://github.com/DragonMinded/libdragon.
The short version:

```sh
git clone https://github.com/DragonMinded/libdragon
cd libdragon
./build-toolchain.sh           # installs gcc-mips64-elf into /opt
export N64_INST=/opt
make install
```

Verify with:

```sh
echo $N64_INST
ls $N64_INST/include/n64.mk
```

apollo-64's Makefile picks libdragon up automatically once `$N64_INST` is
set.

## 2. yaYUL (optional - only if you're changing the AGC source)

`src/rope.c` is committed with a real assembled Luminary099 image. If you
just want to build the ROM with the stock flight software, skip this
section. You only need yaYUL if you want to:

- Swap to a different mission (e.g. Comanche055).
- Tweak the `.agc` source and re-assemble.


```sh
git clone https://github.com/virtualagc/virtualagc
cd virtualagc
make yaYUL                     # builds the assembler
sudo make install              # installs to /usr/local/bin
```

Then assemble a mission. Luminary099 is the recommended starting point
(LM, the part that landed):

```sh
cd /path/to/apollo-64/agc-software/Luminary099
yaYUL MAIN.agc                 # produces MAIN.agc.bin
mv MAIN.agc.bin Luminary099.bin
```

The expected output is exactly 73728 bytes (036000 octal). If yaYUL
prints fatal errors, your `.agc` tree has drifted from upstream - re-pull
from virtualagc.

## 3. Build the ROM

```sh
cd /path/to/apollo-64
make rope MISSION=Luminary099   # regenerate src/rope.c (~3 MB C file)
make                            # produces apollo64.z64
```

Run in an emulator:

```sh
ares apollo64.z64               # or mupen64plus, cen64, etc.
```

Or flash to a flashcart (EverDrive 64, 64drive, SummerCart64).

## Switching missions

```sh
make rope MISSION=Comanche055   # CM (Command Module) instead of LM
make
```

`agc-software/` has both Luminary099 and Comanche055 trees. For other
missions (Artemis072, Sunburst37, etc.), copy them in from virtualagc and
adjust `MISSION=`.

## Tests

Host-side smoke test runs the engine on your dev box without libdragon:

```sh
make -C tests
tests/host_smoke 2000000        # 2M AGC cycles, ~24s simulated time
tests/host_smoke 2000000 rset   # ... and simulate an RSET keypress
```

See `tests/README.md` for what the output means and what it does/doesn't
prove.

Manual on-hardware smoke test plan once libdragon paths are exercised:

1. Boot. PROG should sit at `00` for a few seconds, then settle as the
   AGC's executive comes up.
2. Hit Start (RSET) to clear any boot alarms.
3. Hit L (VERB), 1, 6, R (NOUN), 3, 6, A (ENTR) - this is V16N36, "show
   me the AGC time clock". The R1 register should start ticking.

If R1 isn't ticking after step 3, something is wrong with the timer/PIPA
wiring - check `agc_host.c::ChannelOutput` is firing for channel 010.

## What's NOT yet wired

The engine runs and the DSKY decodes correctly, but several AGC peripherals
are stubbed:

- **IMU CDU/PIPA counters**: needed for any guidance program. `ChannelInput`
  needs to return 1 and call `UnprogrammedIncrement(State, RegPIPAX, 0)`
  (etc.) at the right rates. Currently returns 0 unconditionally.
- **Real-time pacing**: the main loop runs at frame batch granularity.
  For programs that depend on exact AGC timing (P63 lunar descent), wire
  `agc_host_tick(1)` into a 85 kHz timer ISR.
- **Audio**: the iconic 1202 alarm tone is not generated. Easy add via
  libdragon's mixer.

These are in [README.md](README.md)'s status list.
