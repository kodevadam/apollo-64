# Host-side tools. Builds with the system compiler, not the N64 toolchain.

CFLAGS ?= -O2 -Wall -Wextra -std=c99

all: bin2rope trace_yaagc

bin2rope: bin2rope.c
	$(CC) $(CFLAGS) -o $@ $<

trace_yaagc: trace_yaagc.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f bin2rope trace_yaagc

.PHONY: all clean
