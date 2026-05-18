# Host-side tools. Builds with the system compiler, not the N64 toolchain.

CFLAGS ?= -O2 -Wall -Wextra -std=c99

all: bin2rope

bin2rope: bin2rope.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f bin2rope

.PHONY: all clean
