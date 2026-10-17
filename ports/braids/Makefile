# Braids port: upstream reference renders, host and Blackfin builds of the
# port, checksum comparison, pitch check and Blackfin cycle benchmarks. Run
# from this directory inside WSL (g++ for the host targets, the Blackfin
# toolchain in ~/op1-toolchain/bfin/bin for the rest).
#
#   make ref          unmodified upstream, 96 kHz        -> out/ref/*.wav, out/ref.txt
#   make port         the port, 96 kHz                   -> out/port/*.wav, out/port.txt
#   make port88       the port at the OP-1's 88.2 kHz    -> out/port88/*.wav, out/port88.txt
#   make compare      checksums: port = upstream at 96 kHz (every shape)
#   make bfin         the port on Blackfin (bfin-elf-run), 96 and 88.2 kHz:
#                     checksums must equal the host's
#   make pitch        fundamental per pitched shape: upstream rate, 88.2 kHz
#                     retuned, and 88.2 kHz without the retune
#   make test-voice   the OP-1 voice wrapper (op1/): pitch conversion, the
#                     24 -> 128 sample FIFO = direct rendering, host = Blackfin
#   make bench        cycles per 128-sample OP-1 call per shape (bfin-elf-run)
#   make image        the wrapper linked as an OP-1 image (sizes, no
#                     .init_array, no soft-float, libgcc helpers pulled in)
#   make diff-upstream  the port's changes to upstream (diff -u)

MI        ?= ../../mutableinstruments
BFIN_BIN  ?= $(HOME)/op1-toolchain/bfin/bin
BUILD     := build
OUT       := out

WARN := -Wall -Wno-unused-variable -Wno-unused-local-typedefs -Wno-unused-function

REF_SRC  := $(MI)/braids/macro_oscillator.cc $(MI)/braids/analog_oscillator.cc \
            $(MI)/braids/digital_oscillator.cc $(MI)/braids/resources.cc \
            $(MI)/stmlib/utils/random.cc
PORT_SRC := src/macro_oscillator.cc src/analog_oscillator.cc src/digital_oscillator.cc \
            src/resources.cc src/stmlib/random.cc
PORT_HDR := $(wildcard src/*.h src/stmlib/*.h)
HARNESS  := harness/render.cc $(wildcard harness/*.h)

HOST_CXX      := g++
HOST_CXXFLAGS := -O2 -g -std=c++14 $(WARN)

BFIN_CXX  := $(BFIN_BIN)/bfin-elf-g++
BFIN_RUN  := $(BFIN_BIN)/bfin-elf-run
SIM_CXXFLAGS := -O2 -std=c++17 -mcpu=bf524 -msim -fno-exceptions -fno-rtti -DOP1_SIM -I. $(WARN)

.PHONY: all ref port port88 compare bfin pitch test-voice bench image diff-upstream clean

all: compare bfin pitch test-voice

$(BUILD) $(OUT) $(OUT)/ref $(OUT)/port $(OUT)/port88:
	mkdir -p $@

# --- Host ------------------------------------------------------------------

$(BUILD)/render_ref: $(HARNESS) $(REF_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DTEST -DREF -I$(MI) -I. harness/render.cc $(REF_SRC) -o $@

$(BUILD)/render_port: $(HARNESS) $(PORT_SRC) $(PORT_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -I. harness/render.cc $(PORT_SRC) -o $@

ref: $(BUILD)/render_ref | $(OUT)/ref
	$(BUILD)/render_ref -o $(OUT)/ref | tee $(OUT)/ref.txt

port: $(BUILD)/render_port | $(OUT)/port
	$(BUILD)/render_port -o $(OUT)/port | tee $(OUT)/port.txt

port88: $(BUILD)/render_port | $(OUT)/port88
	$(BUILD)/render_port -r 88200 -o $(OUT)/port88 | tee $(OUT)/port88.txt

# Every shape's checksums (first 200 ms and the full 2 s) must match.
compare: ref port
	@if [ "$$(grep '^shape\|^all' $(OUT)/ref.txt)" = "$$(grep '^shape\|^all' $(OUT)/port.txt)" ]; \
	then echo "compare: port = upstream, bit-exact, all 48 shapes (96 kHz)"; \
	else diff <(grep '^shape\|^all' $(OUT)/ref.txt) <(grep '^shape\|^all' $(OUT)/port.txt); \
	     echo "compare: port DIFFERS from upstream"; exit 1; fi

SHELL := /bin/bash

pitch: $(BUILD)/render_ref $(BUILD)/render_port | $(OUT)
	$(BUILD)/render_ref -p | tee $(OUT)/pitch_ref96.txt
	$(BUILD)/render_port -p -r 88200 | tee $(OUT)/pitch_port88.txt
	$(BUILD)/render_port -p -r 88200 -n | tee $(OUT)/pitch_port88_noretune.txt

# --- Blackfin (simulator) --------------------------------------------------

$(BUILD)/render_port.elf: $(HARNESS) $(PORT_SRC) $(PORT_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) harness/render.cc $(PORT_SRC) -o $@

# The simulator renders the first 200 ms of each shape: its "check"
# checksums must equal the host port's (and so upstream's at 96 kHz).
bfin: $(BUILD)/render_port.elf $(BUILD)/render_port | $(OUT)
	$(BFIN_RUN) $< -m 200 | tee $(OUT)/bfin96.txt
	$(BUILD)/render_port -m 200 > $(OUT)/host96_200.txt
	$(BFIN_RUN) $< -m 200 -r 88200 | tee $(OUT)/bfin88.txt
	$(BUILD)/render_port -m 200 -r 88200 > $(OUT)/host88_200.txt
	@ok=1; \
	for r in 96 88; do \
	  if [ "$$(grep '^shape\|^all' $(OUT)/bfin$$r.txt)" = "$$(grep '^shape\|^all' $(OUT)/host$${r}_200.txt)" ]; \
	  then echo "bfin: Blackfin = host at $$r kHz (all 48 shapes)"; \
	  else diff <(grep '^shape\|^all' $(OUT)/bfin$$r.txt) <(grep '^shape\|^all' $(OUT)/host$${r}_200.txt); \
	       echo "bfin: Blackfin DIFFERS from host at $$r kHz"; ok=0; fi; \
	done; [ $$ok = 1 ]

diff-upstream:
	@for f in $(notdir $(wildcard src/*.h src/*.cc)); do \
	  diff -u $(MI)/braids/$$f src/$$f; done; \
	diff -u $(MI)/stmlib/stmlib.h src/stmlib/stmlib.h; \
	diff -u $(MI)/stmlib/utils/dsp.h src/stmlib/dsp.h; \
	diff -u $(MI)/stmlib/utils/random.h src/stmlib/random.h; \
	diff -u $(MI)/stmlib/utils/random.cc src/stmlib/random.cc; true

clean:
	rm -rf $(BUILD) $(OUT)
