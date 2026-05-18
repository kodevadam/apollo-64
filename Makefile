# apollo-64: libdragon Makefile for the N64 build.
#
# Requires libdragon installed and $N64_INST set. See BUILDING.md.
#
# Building from scratch:
#   make rope          # regenerate src/rope.c from agc-software/<mission>.bin
#   make               # produce apollo64.z64
#   make clean
#
# Default rope is all-zeros; build will succeed but the AGC will boot into
# alarm. For a runnable mission, run `make rope` after building Luminary099
# with yaYUL.

BUILD_DIR = build
ROM_NAME  = apollo64
MISSION   = Luminary099

include $(N64_INST)/include/n64.mk

# -DN64 picks the apollo-64 branch in the vendored yaAGC headers. The two
# patched headers and the engine itself are pure C; no other defines needed.
N64_CFLAGS += -DN64 -Ivendor/yaAGC -Isrc -Wno-unused-parameter

src = \
  src/main.c \
  src/agc_host.c \
  src/dsky.c \
  src/input.c \
  src/rope.c \
  vendor/yaAGC/agc_engine.c

# (Assets pipeline goes here once we have artwork. For now the renderer
# uses libdragon's built-in font, so the DFS is empty.)
assets_conv =

all: $(ROM_NAME).z64

$(BUILD_DIR)/$(ROM_NAME).dfs: $(assets_conv)
	$(N64_MKDFS) $@ filesystem || $(N64_MKDFS) $@

$(BUILD_DIR)/$(ROM_NAME).elf: $(src:%.c=$(BUILD_DIR)/%.o)

$(ROM_NAME).z64: N64_ROM_TITLE = "Apollo-64"
$(ROM_NAME).z64: $(BUILD_DIR)/$(ROM_NAME).dfs

# Regenerate rope.c from a pre-assembled .bin. Run yaYUL yourself first;
# see BUILDING.md.
.PHONY: rope
rope: tools/bin2rope
	tools/bin2rope agc-software/$(MISSION)/$(MISSION).bin > src/rope.c
	@echo "Regenerated src/rope.c from $(MISSION).bin"

tools/bin2rope:
	$(MAKE) -C tools

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) $(ROM_NAME).z64 filesystem
	$(MAKE) -C tools clean

-include $(wildcard $(BUILD_DIR)/*.d) $(wildcard $(BUILD_DIR)/*/*.d)
