# Host-side tests. Built with the system compiler, not the N64 toolchain.
# Drives the AGC engine on the host so the pipeline can be smoke-tested
# without flashing a ROM.

# -Wno-format silences a noisy warning about FORMAT_64U being "%llu" while
# host x86-64 uint64_t is "unsigned long". On the N64 MIPS target uint64_t is
# "unsigned long long" so "%llu" is correct - the warning is a host-only
# artefact of running test code we'd never compile this way for real.
CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format \
          -DN64 -I../vendor/yaAGC -I../src

SOURCES = \
  host_smoke.c \
  ../src/agc_host.c \
  ../src/dsky_decode.c \
  ../src/rope.c \
  ../vendor/yaAGC/agc_engine.c

all: host_smoke

host_smoke: $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

run: host_smoke
	./host_smoke

clean:
	rm -f host_smoke

.PHONY: all run clean
