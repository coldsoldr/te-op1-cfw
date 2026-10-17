# Clouds port: reference renders, host builds of the port, comparison and
# Blackfin benchmarks. Run from this directory inside WSL (needs g++, and
# bfin-elf-g++ for the bench targets; see tools/build_bfin_gcc.sh).
#
#   make ref          upstream float Clouds (32 kHz scenarios)  -> out/ref/*.wav
#   make copy         the float copy in src/dsp                 -> out/copy/*.wav
#   make copy-check   the copy must be bit-identical to ref (checksums)
#   make port         the fixed-point port, upstream constants  -> out/port/*.wav
#   make compare      waveform SNR and band spectra, port vs ref
#   make port44       the port with the OP-1 (44.1 kHz) constants -> out/port44/*.wav
#   make test         unit checks (kernels C = assembly = host, ring-buffer wraps)
#   make bench        the port on Blackfin (bfin-elf-run), cycles per mode
#   make sim-fx       the OP-1 effect entry points (op1/clouds_fx.cc) and the
#                     settings/click round trips in the simulator
#
# Rings is shared by path, read-only: ../rings/src/fixed.h, ffloat.h,
# fixed_tables.*, reverb_fixed.* (mono reverb), and ../rings/harness
# (cycles.h, compare.py). Nothing under ../rings is modified.

SHELL     := /bin/bash
MI        ?= ../../mutableinstruments
RINGS     := ../rings
BFIN_BIN  ?= $(HOME)/op1-toolchain/bfin/bin
PYTHON    ?= python3
BUILD     := build
OUT       := out
COMPARE   := $(RINGS)/harness/compare.py

STMLIB_SRC := $(MI)/stmlib/dsp/units.cc $(MI)/stmlib/dsp/atan.cc $(MI)/stmlib/utils/random.cc
REF_SRC    := $(MI)/clouds/dsp/granular_processor.cc $(MI)/clouds/dsp/correlator.cc \
              $(MI)/clouds/dsp/mu_law.cc $(MI)/clouds/dsp/pvoc/frame_transformation.cc \
              $(MI)/clouds/dsp/pvoc/phase_vocoder.cc $(MI)/clouds/dsp/pvoc/stft.cc \
              $(MI)/clouds/resources.cc $(STMLIB_SRC)
COPY_SRC   := src/dsp/granular_processor.cc src/dsp/correlator.cc src/dsp/mu_law.cc \
              src/dsp/pvoc/frame_transformation.cc src/dsp/pvoc/phase_vocoder.cc \
              src/dsp/pvoc/stft.cc src/resources.cc $(STMLIB_SRC)
CORE_SRC   := src/core/clouds_core.cc src/core/granular_player.cc src/core/reverb_stereo.cc src/core/diffuser_fixed.cc \
              src/core/clouds_tables.cc src/core/halfband.cc $(RINGS)/src/fixed_tables.cc
CORE_ASM   := src/core/grain_kernels_bfin.S src/core/reverb_stereo_bfin.S src/core/diffuser_bfin.S \
              src/core/halfband_bfin.S
CORE_HDR   := $(wildcard src/core/*.h) $(RINGS)/src/fixed.h $(RINGS)/src/ffloat.h
HARNESS    := harness/render.cc $(wildcard harness/*.h)

WARN          := -Wall -Wno-unused-variable -Wno-unused-local-typedefs -Wno-unused-function \
                 -Wno-class-memaccess
INC           := -I. -I.. -I$(RINGS) -I$(MI)
HOST_CXX      := g++
HOST_CXXFLAGS := -O2 -g -std=c++17 -DTEST $(INC) $(WARN)

BFIN_CXX      := $(BFIN_BIN)/bfin-elf-g++
BFIN_RUN      := $(BFIN_BIN)/bfin-elf-run
BFIN_OBJDUMP  := $(BFIN_BIN)/bfin-elf-objdump
SIM_CXXFLAGS  := -O2 -std=c++17 -mcpu=bf524 -msim -fno-exceptions -fno-rtti -DOP1_SIM \
                 $(INC) $(WARN)

# Comparable builds use upstream's constants (32 kHz in samples); the OP-1
# profile scales the time constants to 44.1 kHz (src/core/config.h).
RATE32 := -DOP1CLOUDS_SAMPLE_RATE=32000 -DOP1RINGS_SAMPLE_RATE=32000
RATE44 := -DOP1CLOUDS_SAMPLE_RATE=44100 -DOP1RINGS_SAMPLE_RATE=44100
# The OP-1 image's core rate (op1/clouds_fx.cc: half the effect rate).
RATE22 := -DOP1CLOUDS_SAMPLE_RATE=22050 -DOP1RINGS_SAMPLE_RATE=44100

.PHONY: all ref copy copy-check copyd port port44 compare spec test test-governor profile bench bench-ref sim-fx clean

all: ref port compare

$(BUILD) $(OUT)/ref $(OUT)/copy $(OUT)/port $(OUT)/port44:
	mkdir -p $@

$(BUILD)/render_ref: $(HARNESS) $(REF_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DREF harness/render.cc $(REF_SRC) -o $@ -lm

$(BUILD)/render_copy: $(HARNESS) $(COPY_SRC) $(wildcard src/dsp/*.h src/dsp/*/*.h src/*.h) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DCOPY harness/render.cc $(COPY_SRC) -o $@ -lm

$(BUILD)/render_port: $(HARNESS) $(CORE_SRC) $(CORE_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(RATE32) -DPORT harness/render.cc $(CORE_SRC) -o $@ -lm

$(BUILD)/render_port44: $(HARNESS) $(CORE_SRC) $(CORE_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(RATE44) -DPORT harness/render.cc $(CORE_SRC) -o $@ -lm

ref: $(BUILD)/render_ref | $(OUT)/ref
	$(BUILD)/render_ref $(OUT)/ref $(S) | tee $(BUILD)/ref.txt

copy: $(BUILD)/render_copy | $(OUT)/copy
	$(BUILD)/render_copy $(OUT)/copy $(S) | tee $(BUILD)/copy.txt

#   make copyd        diagnostics: the copy with the looper's and pitch
#                     shifter's phases and delays in double precision
#                     -> out/copyd (upstream's float rounding drift removed)
$(OUT)/copyd:
	mkdir -p $@

$(BUILD)/render_copyd: $(HARNESS) $(COPY_SRC) $(wildcard src/dsp/*.h src/dsp/*/*.h src/*.h) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DCOPY -DOP1CLOUDS_COPY_REAL=double harness/render.cc $(COPY_SRC) -o $@ -lm

copyd: $(BUILD)/render_copyd | $(OUT)/copyd
	$(BUILD)/render_copyd $(OUT)/copyd $(S) | tee $(BUILD)/copyd.txt

copy-check: ref copy
	@if cmp -s $(BUILD)/ref.txt $(BUILD)/copy.txt; then echo "copy = upstream (bit-identical)"; \
	else echo "copy DIFFERS from upstream"; exit 1; fi

port: $(BUILD)/render_port | $(OUT)/port
	$(BUILD)/render_port $(OUT)/port $(S) | tee $(BUILD)/port.txt

port44: $(BUILD)/render_port44 | $(OUT)/port44
	$(BUILD)/render_port44 $(OUT)/port44 $(S) | tee $(BUILD)/port44.txt

compare:
	$(PYTHON) $(COMPARE) dirdiff $(OUT)/ref $(OUT)/port

spec:
	$(PYTHON) $(COMPARE) dirspec $(OUT)/ref $(OUT)/port

# --- Blackfin ------------------------------------------------------------
#   make bench        the port's scenarios on Blackfin at the OP-1 profile:
#                     cycles per 32-sample block (MHz at 44.1 kHz), and the
#                     output checksums, which must equal the host build's
#                     (build/render_port44_bench_host runs the same scenarios)
BENCH_SECONDS ?= 1.0
$(BUILD)/bench_port.elf: $(HARNESS) $(CORE_SRC) $(CORE_ASM) $(CORE_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) $(RATE44) -DPORT -DBENCH harness/render.cc $(CORE_SRC) $(CORE_ASM) -o $@

$(BUILD)/bench_port_host: $(HARNESS) $(CORE_SRC) $(CORE_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(RATE44) -DPORT -DBENCH harness/render.cc $(CORE_SRC) -o $@ -lm

bench: $(BUILD)/bench_port.elf $(BUILD)/bench_port_host
	@$(BFIN_RUN) $< "$(S)" $(BENCH_SECONDS) | tee $(BUILD)/bench.txt
	@$(BUILD)/bench_port_host "$(S)" $(BENCH_SECONDS) > $(BUILD)/bench_host.txt
	@if [ "$$(grep checksum $(BUILD)/bench.txt)" = "$$(grep checksum $(BUILD)/bench_host.txt)" ]; \
	then echo "bench: Blackfin output = host output"; else echo "bench: Blackfin output DIFFERS from host"; \
	diff <(grep checksum $(BUILD)/bench.txt) <(grep checksum $(BUILD)/bench_host.txt); exit 1; fi

#   make bench-ref    upstream float Clouds on Blackfin (soft-float), for scale
$(BUILD)/bench_ref.elf: $(HARNESS) $(REF_SRC) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) -DTEST -DREF -DBENCH harness/render.cc $(REF_SRC) -o $@ -lm

bench-ref: $(BUILD)/bench_ref.elf
	$(BFIN_RUN) $< "$(S)" 0.05

#   make test         kernel and ring-buffer checks: host build, Blackfin
#                     build (assembly), and the checksums compared
TEST_SRC := tests/test_kernels.cc src/core/reverb_stereo.cc src/core/diffuser_fixed.cc src/core/clouds_tables.cc src/core/halfband.cc $(RINGS)/src/fixed_tables.cc
$(BUILD)/test_kernels.elf: $(TEST_SRC) $(CORE_ASM) $(CORE_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) $(RATE44) $(TEST_SRC) $(CORE_ASM) -o $@

$(BUILD)/test_kernels_host: $(TEST_SRC) $(CORE_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(RATE44) $(TEST_SRC) -o $@ -lm

test: $(BUILD)/test_kernels.elf $(BUILD)/test_kernels_host
	@$(BFIN_RUN) $< > $(BUILD)/test_bfin.txt; s=$$?; cat $(BUILD)/test_bfin.txt; [ $$s = 0 ] || exit $$s
	@$(BUILD)/test_kernels_host > $(BUILD)/test_host.txt; s=$$?; sed 's/^/host  /' $(BUILD)/test_host.txt; [ $$s = 0 ] || exit $$s
	@if [ "$$(grep checksum $(BUILD)/test_bfin.txt)" = "$$(grep checksum $(BUILD)/test_host.txt)" ]; \
	then echo "test: Blackfin (assembly) = host (C)"; else echo "test: Blackfin DIFFERS from host"; exit 1; fi

#   make test-governor  the CPU governor against a simulated OP-1
$(BUILD)/test_governor: tests/test_governor.cc src/core/governor.h | $(BUILD)
	$(HOST_CXX) -O2 -std=c++17 -I. $(WARN) tests/test_governor.cc -o $@

test-governor: $(BUILD)/test_governor
	$<

#   make profile      cycles per configuration on Blackfin (tests/profile_core.cc)
$(BUILD)/profile_core.elf: tests/profile_core.cc $(CORE_SRC) $(CORE_ASM) $(CORE_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) -g $(RATE44) tests/profile_core.cc $(CORE_SRC) $(CORE_ASM) -o $@

PROFILE_BLOCKS ?= 900
profile: $(BUILD)/profile_core.elf
	$(BFIN_RUN) $< "$(S)" $(PROFILE_BLOCKS)

#   make sim-fx       op1/clouds_fx.cc's mono and stereo entry points in the
#                     simulator, at the OP-1's call rate (64 samples, 44.1 kHz)
SIM_FX_SRC := tests/sim_fx.cc op1/clouds_fx.cc $(CORE_SRC) $(CORE_ASM)
$(BUILD)/sim_fx.elf: $(SIM_FX_SRC) $(CORE_HDR) $(wildcard op1/*.h) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) $(RATE22) $(SIM_FX_SRC) -o $@

sim-fx: $(BUILD)/sim_fx.elf
	$(BFIN_RUN) $<

clean:
	rm -rf $(BUILD) $(OUT)
