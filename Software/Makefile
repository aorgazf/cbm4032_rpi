# CBM 4032 IEEE-488 HAT - stage 1 bring-up tools
#
#   make                 build on the Pi (or any Linux host; use --backend sim there)
#   make test            unit tests and the self-test on the simulated board
#   make CC=aarch64-linux-gnu-gcc   cross-compile for Raspberry Pi OS 64-bit
#   sudo make install    install the tools to $(PREFIX)/bin

CC      ?= gcc
CFLAGS  ?= -O2 -g
HAT_CFLAGS := -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Iinclude -pthread
LDFLAGS += -pthread
PREFIX  ?= /usr/local
BUILD   ?= build

LIB_SRC := src/gpio_backend.c src/gpio_bcm2711.c src/gpio_rp1.c src/gpio_sim.c \
           src/hat_board.c src/rt_util.c src/bus_monitor.c
LIB_OBJ := $(LIB_SRC:src/%.c=$(BUILD)/%.o)
TOOLS   := hat_bench hat_selftest hat_monitor hat_safe
BINS    := $(TOOLS:%=$(BUILD)/%)

all: $(BINS)

$(BUILD)/%.o: src/%.c include/*.h | $(BUILD)
	$(CC) $(HAT_CFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/libhat.a: $(LIB_OBJ)
	$(AR) rcs $@ $^

$(BUILD)/%: tools/%.c $(BUILD)/libhat.a include/*.h
	$(CC) $(HAT_CFLAGS) $(CFLAGS) -o $@ $< $(BUILD)/libhat.a $(LDFLAGS)

$(BUILD)/test_hat: tests/test_hat.c tests/pet_model.c tests/pet_model.h $(BUILD)/libhat.a
	$(CC) $(HAT_CFLAGS) $(CFLAGS) -Itests -o $@ tests/test_hat.c tests/pet_model.c $(BUILD)/libhat.a $(LDFLAGS)

$(BUILD):
	mkdir -p $@

test: $(BUILD)/test_hat $(BUILD)/hat_selftest $(BUILD)/hat_safe
	$(BUILD)/test_hat
	$(BUILD)/hat_selftest --backend sim
	$(BUILD)/hat_safe --backend sim

install: $(BINS)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BINS) $(DESTDIR)$(PREFIX)/bin

clean:
	rm -rf $(BUILD)

.PHONY: all test install clean
