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

all: host_smoke dsky_decode_test input_map_test input_poll_test selftest equiv

host_smoke: $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

# The self-check test boots the AGC and links the engine, like host_smoke.
selftest: selftest.c ../src/agc_host.c ../src/rope.c ../vendor/yaAGC/agc_engine.c
	$(CC) $(CFLAGS) -o $@ selftest.c ../src/agc_host.c ../src/rope.c \
	  ../vendor/yaAGC/agc_engine.c

# The equivalence / determinism harness. Links the DSKY decoder too, so
# each scenario can assert the facet it exercises against the decoded panel.
equiv: equiv.c ../src/agc_host.c ../src/dsky_decode.c ../src/rope.c \
       ../vendor/yaAGC/agc_engine.c
	$(CC) $(CFLAGS) -o $@ equiv.c ../src/agc_host.c ../src/dsky_decode.c \
	  ../src/rope.c ../vendor/yaAGC/agc_engine.c

# The decode test only needs the pure-C decoder + the agc_engine.h header
# (for DSKY_* constants). No engine link required.
dsky_decode_test: dsky_decode_test.c ../src/dsky_decode.c
	$(CC) $(CFLAGS) -o $@ dsky_decode_test.c ../src/dsky_decode.c

# The input-map test only needs the pure-C mapper. No engine, no libdragon.
input_map_test: input_map_test.c ../src/input_map.c
	$(CC) $(CFLAGS) -o $@ input_map_test.c ../src/input_map.c

# The input-poll test compiles the real src/input.c against a mock
# libdragon joypad (tests/mock/libdragon.h takes precedence via -Imock).
input_poll_test: input_poll_test.c ../src/input.c ../src/input_map.c
	$(CC) $(CFLAGS) -Imock -o $@ input_poll_test.c ../src/input.c ../src/input_map.c

run: host_smoke
	./host_smoke

run-decode-test: dsky_decode_test
	./dsky_decode_test

run-input-test: input_map_test
	./input_map_test

run-input-poll-test: input_poll_test
	./input_poll_test

run-selftest: selftest
	./selftest
	./selftest corrupt

run-equiv: equiv
	./equiv

clean:
	rm -f host_smoke dsky_decode_test input_map_test input_poll_test \
	      selftest equiv

.PHONY: all run run-decode-test run-input-test run-input-poll-test \
        run-selftest run-equiv clean
