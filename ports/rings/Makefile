# Rings port: reference renders, host builds of the port, comparison and
# Blackfin benchmarks. Run from this directory inside WSL (needs g++, and
# bfin-elf-g++ for the bench targets; see tools/build_bfin_gcc.sh).
#
#   make ref          upstream float Rings, 48 kHz           -> out/ref/*.wav
#   make port         the port built at 48 kHz (comparable)   -> out/port/*.wav
#   make port44       the port at the OP-1 rate (44.1 kHz)    -> out/port44/*.wav
#   make compare      SNR of out/port against out/ref
#   make bench-ref    upstream float Rings on Blackfin (bfin-elf-run), cycles per block
#   make bench-port   the port on Blackfin, cycles per block

MI        ?= ../../mutableinstruments
BFIN_BIN  ?= $(HOME)/op1-toolchain/bfin/bin
PYTHON    ?= python3
BUILD     := build
OUT       := out

STMLIB_SRC := $(MI)/stmlib/dsp/units.cc $(MI)/stmlib/utils/random.cc
REF_SRC    := $(MI)/rings/dsp/part.cc $(MI)/rings/dsp/resonator.cc \
              $(MI)/rings/dsp/string.cc $(MI)/rings/dsp/fm_voice.cc \
              $(MI)/rings/resources.cc $(STMLIB_SRC)
PORT_SRC   := src/part.cc src/resonator.cc src/modal_resonator.cc src/fixed_tables.cc \
              src/string.cc src/fm_voice.cc \
              src/resources.cc $(STMLIB_SRC)
PORT_HDR   := $(wildcard src/*.h src/fx/*.h)
HARNESS    := harness/render.cc $(wildcard harness/*.h)

WARN          := -Wall -Wno-unused-variable -Wno-unused-local-typedefs -Wno-unused-function
HOST_CXX      := g++
HOST_CXXFLAGS := -O2 -g -DTEST -I$(MI) -I. $(WARN)

BFIN_CXX      := $(BFIN_BIN)/bfin-elf-g++
BFIN_RUN      := $(BFIN_BIN)/bfin-elf-run
BFIN_CXXFLAGS := -O2 -mcpu=bf524 -msim -fno-exceptions -fno-rtti -DTEST -DBENCH \
                 -I$(MI) -I. $(WARN)
BENCH_SECONDS ?= 0.25

.PHONY: all ref port port44 compare bench-ref bench-port clean

all: ref port compare

$(BUILD) $(OUT)/ref $(OUT)/port $(OUT)/port44:
	mkdir -p $@

$(BUILD)/render_ref: $(HARNESS) $(REF_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) harness/render.cc $(REF_SRC) -o $@ -lm

$(BUILD)/render_port: $(HARNESS) $(PORT_SRC) $(PORT_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DPORT -DOP1RINGS_SAMPLE_RATE=48000 harness/render.cc $(PORT_SRC) -o $@ -lm

$(BUILD)/render_port44: $(HARNESS) $(PORT_SRC) $(PORT_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DPORT -DOP1RINGS_SAMPLE_RATE=44100 harness/render.cc $(PORT_SRC) -o $@ -lm

ref: $(BUILD)/render_ref | $(OUT)/ref
	$(BUILD)/render_ref $(OUT)/ref

port: $(BUILD)/render_port | $(OUT)/port
	$(BUILD)/render_port $(OUT)/port

port44: $(BUILD)/render_port44 | $(OUT)/port44
	$(BUILD)/render_port44 $(OUT)/port44

compare:
	$(PYTHON) harness/compare.py dirdiff $(OUT)/ref $(OUT)/port

$(BUILD)/bench_ref.elf: $(HARNESS) $(REF_SRC) | $(BUILD)
	$(BFIN_CXX) $(BFIN_CXXFLAGS) harness/render.cc $(REF_SRC) -o $@ -lm

$(BUILD)/bench_port.elf: $(HARNESS) $(PORT_SRC) $(PORT_HDR) | $(BUILD)
	$(BFIN_CXX) $(BFIN_CXXFLAGS) -DPORT harness/render.cc $(PORT_SRC) -o $@ -lm

bench-ref: $(BUILD)/bench_ref.elf
	$(BFIN_RUN) $< $(BENCH_SECONDS)

bench-port: $(BUILD)/bench_port.elf
	$(BFIN_RUN) $< $(BENCH_SECONDS)

# --- Images injected into the OP-1 app (tools/inject_app.py) -------------
#   make op1-probe    CPU/memory probe (op1/probe.cc)  -> build/op1_probe.elf
#   make op1-engine   Rings engine (op1/engine.cc)     -> build/op1_engine.elf

OP1_CXXFLAGS := -O2 -std=c++17 -mcpu=bf524 -ffreestanding -fno-exceptions \
                -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
                -ffunction-sections -fdata-sections -I. $(WARN)
OP1_LDFLAGS  := -nostdlib -nostartfiles -T op1/link.ld -Wl,--gc-sections \
                -Wl,--no-undefined
OP1_ENGINE_SRC := op1/engine.cc op1/support.cc src/op1_voice.cc \
                  src/modal_resonator.cc src/fixed_tables.cc src/mode_bank_bfin.S

.PHONY: op1-probe op1-engine sim-engine host-probe step-params

$(BUILD)/op1_probe.elf: op1/probe.cc op1/support.cc op1/op1.h op1/link.ld | $(BUILD)
	$(BFIN_CXX) $(OP1_CXXFLAGS) op1/probe.cc op1/support.cc $(OP1_LDFLAGS) -o $@ -lgcc
	$(BFIN_BIN)/bfin-elf-size -A $@

$(BUILD)/op1_engine.elf: $(OP1_ENGINE_SRC) op1/op1.h op1/param_smoother.h op1/link.ld $(PORT_HDR) | $(BUILD)
	$(BFIN_CXX) $(OP1_CXXFLAGS) $(OP1_ENGINE_SRC) $(OP1_LDFLAGS) -o $@ -lgcc
	$(BFIN_BIN)/bfin-elf-size -A $@

op1-probe: $(BUILD)/op1_probe.elf
op1-engine: $(BUILD)/op1_engine.elf

# Checks of the images' code off the device:
#   make sim-engine   the engine's entry points in bfin-elf-run: CPU cost, and
#                     voice 0's output -> build/sim_engine.raw (s16le, 88.2 kHz)
#   make host-probe   the probe against a simulated OP-1 -> build/host_probe.wav
#                     (decode with tools/decode_probe_beeps.py)
SIM_CXXFLAGS := -O2 -std=c++17 -mcpu=bf524 -msim -fno-exceptions -fno-rtti \
                -DOP1_SIM -I. $(WARN)
SIM_ENGINE_SRC := tests/sim_engine.cc op1/engine.cc src/op1_voice.cc \
                  src/modal_resonator.cc src/fixed_tables.cc src/mode_bank_bfin.S

$(BUILD)/sim_engine.elf: $(SIM_ENGINE_SRC) op1/op1.h op1/param_smoother.h $(PORT_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) $(SIM_ENGINE_SRC) -o $@

sim-engine: $(BUILD)/sim_engine.elf
	$(BFIN_RUN) $< $(BUILD)/sim_engine.raw

$(BUILD)/host_probe: tests/host_probe.cc op1/probe.cc op1/op1.h | $(BUILD)
	$(HOST_CXX) -O2 -std=c++17 -DOP1_HOST_SIM -DOP1_SIM -I. $(WARN) tests/host_probe.cc op1/probe.cc -o $@

#   make step-params  clicks from parameter steps, upstream vs port, and a
#                     check that a sustained note has no discontinuities
STEP_PARAMS_SRC := tests/step_params.cc src/modal_resonator.cc src/op1_voice.cc \
                   src/fixed_tables.cc $(MI)/rings/dsp/resonator.cc \
                   $(MI)/rings/resources.cc $(MI)/stmlib/dsp/units.cc

$(BUILD)/step_params: $(STEP_PARAMS_SRC) op1/param_smoother.h $(PORT_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(STEP_PARAMS_SRC) -o $@ -lm

step-params: $(BUILD)/step_params
	$<

HOST_PROBE_ARGS ?= 400 40 1 0
host-probe: $(BUILD)/host_probe
	$(BUILD)/host_probe $(BUILD)/host_probe.wav $(HOST_PROBE_ARGS)

clean:
	rm -rf $(BUILD) $(OUT)
