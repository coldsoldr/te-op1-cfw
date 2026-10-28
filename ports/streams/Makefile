# Streams port: copy check, reference and port renders, comparison, Blackfin
# bit-exactness and cycle benchmarks. Run from this directory inside WSL:
#   wsl -e bash -lc 'cd /mnt/<drive>/<repo>/ports/streams && make ...'
#
#   make tables      check the table generator against upstream (31089 Hz),
#                    regenerate src/dsp/resources_44k.cc
#   make copy-check  upstream processors vs the copy, 31089 Hz, bit for bit
#                    (as upstream, and with the exact speed-ups)
#   make ref         reference: upstream processors at 31089 Hz driving a
#                    float VCA + SVF model at 44.1 kHz     -> out/ref/*.wav
#   make port        the fixed-point port at 44.1 kHz, 64-sample blocks
#                                                          -> out/port/*.wav
#   make compare     port vs ref: output SNR, gain and cutoff tracking
#   make test        host vs Blackfin (bfin-elf-run) checksums: processors
#                    unmodified (31089) and retimed (44.1k), the effect core
#                    mono and stereo; the OP-1 entry points in the simulator
#   make bench       simulated cycles per 64-sample call, mono and stereo
#   make sim-fx      op1/streams_fx.cc entry points in bfin-elf-run (OP1_SIM)
#   make op1-size    link the entry points freestanding (no package): code
#                    and data size, no static constructors

MI        ?= ../../mutableinstruments
RINGS     ?= ../rings
BFIN_BIN  ?= $(HOME)/op1-toolchain/bfin/bin
PYTHON    ?= python3
BUILD     := build
OUT       := out

WARN := -Wall -Wno-unused-variable -Wno-unused-local-typedefs -Wno-unused-function \
        -Wno-class-memaccess

HOST_CXX      := g++
HOST_CXXFLAGS := -O2 -g -std=c++14 -I$(MI) -I. $(WARN)
BFIN_CXX      := $(BFIN_BIN)/bfin-elf-g++
BFIN_RUN      := $(BFIN_BIN)/bfin-elf-run
BFIN_SIZE     := $(BFIN_BIN)/bfin-elf-size
BFIN_OBJDUMP  := $(BFIN_BIN)/bfin-elf-objdump
BFIN_CXXFLAGS := -O2 -std=c++14 -mcpu=bf524 -msim -fno-exceptions -fno-rtti \
                 -I$(MI) -I. $(WARN)

UP_SRC  := $(MI)/streams/processor.cc $(MI)/streams/envelope.cc \
           $(MI)/streams/vactrol.cc $(MI)/streams/follower.cc \
           $(MI)/streams/compressor.cc $(MI)/streams/lorenz_generator.cc \
           $(MI)/streams/svf.cc $(MI)/streams/resources.cc
DSP_SRC := src/dsp/processor.cc src/dsp/envelope.cc src/dsp/vactrol.cc \
           src/dsp/follower.cc src/dsp/compressor.cc src/dsp/lorenz_generator.cc \
           src/dsp/svf.cc src/dsp/resources.cc src/dsp/resources_44k.cc src/dsp/resources_22k.cc
DSP_HDR := $(wildcard src/dsp/*.h)

# Rate and speed-up configurations (src/dsp/retime.h).
CFG_UNMOD := -DOP1STREAMS_SAMPLE_RATE=31089 -DOP1STREAMS_FAST=0 -DOP1STREAMS_CENTROID_STRIDE=1
CFG_EXACT := -DOP1STREAMS_SAMPLE_RATE=31089 -DOP1STREAMS_FAST=1 -DOP1STREAMS_CENTROID_STRIDE=1
CFG_PORT  := -DOP1STREAMS_SAMPLE_RATE=44100 -DOP1STREAMS_FAST=1 -DOP1STREAMS_CENTROID_STRIDE=8
# The cheap profile: processors on every other sample.
CFG_PORT22 := -DOP1STREAMS_SAMPLE_RATE=22050 -DOP1STREAMS_FAST=1 -DOP1STREAMS_CENTROID_STRIDE=4

.PHONY: all tables copy-check ref port compare test bench sim-fx op1-size clean

all: copy-check ref port compare

$(BUILD) $(OUT)/ref $(OUT)/port:
	mkdir -p $@

tables:
	$(PYTHON) resources/gen_tables.py check $(MI)/streams/resources.cc
	$(PYTHON) resources/gen_tables.py write src/dsp/resources_44k.cc
	$(PYTHON) resources/gen_tables.py write src/dsp/resources_22k.cc 22050

# --- Copy check ------------------------------------------------------------
$(BUILD)/copy_check0: tests/copy_check.cc harness/drive.h harness/signals.h $(UP_SRC) $(DSP_SRC) $(DSP_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(CFG_UNMOD) tests/copy_check.cc $(UP_SRC) $(DSP_SRC) -o $@

$(BUILD)/copy_check1: tests/copy_check.cc harness/drive.h harness/signals.h $(UP_SRC) $(DSP_SRC) $(DSP_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(CFG_EXACT) tests/copy_check.cc $(UP_SRC) $(DSP_SRC) -o $@

copy-check: $(BUILD)/copy_check0 $(BUILD)/copy_check1
	$(BUILD)/copy_check0
	$(BUILD)/copy_check1

# --- Reference, port, comparison --------------------------------------------
CORE_SRC := src/core/streams_core.cc src/core/core_tables.cc
CORE_HDR := $(wildcard src/core/*.h)
HARNESS_HDR := $(wildcard harness/*.h)

$(BUILD)/render: harness/render.cc $(HARNESS_HDR) $(UP_SRC) $(DSP_SRC) $(DSP_HDR) $(CORE_SRC) $(CORE_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(CFG_PORT) harness/render.cc $(UP_SRC) $(DSP_SRC) $(CORE_SRC) -o $@ -lm

input: $(BUILD)/render | $(OUT)/input
	$(BUILD)/render input $(OUT)/input

ref: $(BUILD)/render | $(OUT)/ref
	$(BUILD)/render ref $(OUT)/ref

port: $(BUILD)/render | $(OUT)/port
	$(BUILD)/render port $(OUT)/port

compare: $(BUILD)/render
	$(BUILD)/render compare $(OUT)/ref $(OUT)/port

# The cheap profile (processors at 22.05 kHz) against the same reference.
$(BUILD)/render22: harness/render.cc $(HARNESS_HDR) $(UP_SRC) $(DSP_SRC) $(DSP_HDR) $(CORE_SRC) $(CORE_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(CFG_PORT22) harness/render.cc $(UP_SRC) $(DSP_SRC) $(CORE_SRC) -o $@ -lm

port22: $(BUILD)/render22 | $(OUT)/port22
	$(BUILD)/render22 port $(OUT)/port22

compare22: $(BUILD)/render22
	$(BUILD)/render22 compare $(OUT)/ref $(OUT)/port22

$(OUT)/port22:
	mkdir -p $@

# Phase-blind band spectra (Rings' stdlib-only compare.py; slow).
spec:
	$(PYTHON) $(RINGS)/harness/compare.py dirspec $(OUT)/ref $(OUT)/port

$(OUT)/input:
	mkdir -p $@

# --- Host vs Blackfin checksums, cycles -------------------------------------
BENCH_SECONDS ?= 1
BENCH_DEPS := tests/bench.cc $(HARNESS_HDR) $(DSP_SRC) $(DSP_HDR) $(CORE_SRC) $(CORE_HDR)
BENCH_PROC := -DBENCH_PROCESSORS
BENCH_CORE := -DBENCH_CORE -DOP1STREAMS_PROFILE

$(BUILD)/bench_unmod_host: $(BENCH_DEPS) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(CFG_UNMOD) $(BENCH_PROC) tests/bench.cc $(DSP_SRC) $(CORE_SRC) -o $@
$(BUILD)/bench_unmod.elf: $(BENCH_DEPS) | $(BUILD)
	$(BFIN_CXX) $(BFIN_CXXFLAGS) $(CFG_UNMOD) $(BENCH_PROC) tests/bench.cc $(DSP_SRC) $(CORE_SRC) -o $@
$(BUILD)/bench_port_host: $(BENCH_DEPS) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(CFG_PORT) $(BENCH_PROC) $(BENCH_CORE) tests/bench.cc $(DSP_SRC) $(CORE_SRC) -o $@
$(BUILD)/bench_port.elf: $(BENCH_DEPS) | $(BUILD)
	$(BFIN_CXX) $(BFIN_CXXFLAGS) $(CFG_PORT) $(BENCH_PROC) $(BENCH_CORE) tests/bench.cc $(DSP_SRC) $(CORE_SRC) -o $@

$(BUILD)/bench_port22_host: $(BENCH_DEPS) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(CFG_PORT22) $(BENCH_PROC) $(BENCH_CORE) tests/bench.cc $(DSP_SRC) $(CORE_SRC) -o $@
$(BUILD)/bench_port22.elf: $(BENCH_DEPS) | $(BUILD)
	$(BFIN_CXX) $(BFIN_CXXFLAGS) $(CFG_PORT22) $(BENCH_PROC) $(BENCH_CORE) tests/bench.cc $(DSP_SRC) $(CORE_SRC) -o $@
$(BUILD)/bench_port22_host.txt: $(BUILD)/bench_port22_host
	$< $(BENCH_SECONDS) > $@
$(BUILD)/bench_port22_bfin.txt: $(BUILD)/bench_port22.elf
	$(BFIN_RUN) $< $(BENCH_SECONDS) > $@

$(BUILD)/bench_unmod_host.txt: $(BUILD)/bench_unmod_host
	$< $(BENCH_SECONDS) > $@
$(BUILD)/bench_unmod_bfin.txt: $(BUILD)/bench_unmod.elf
	$(BFIN_RUN) $< $(BENCH_SECONDS) > $@
$(BUILD)/bench_port_host.txt: $(BUILD)/bench_port_host
	$< $(BENCH_SECONDS) > $@
$(BUILD)/bench_port_bfin.txt: $(BUILD)/bench_port.elf
	$(BFIN_RUN) $< $(BENCH_SECONDS) > $@

bench-check: $(BUILD)/bench_unmod_host.txt $(BUILD)/bench_unmod_bfin.txt $(BUILD)/bench_port_host.txt $(BUILD)/bench_port_bfin.txt              $(BUILD)/bench_port22_host.txt $(BUILD)/bench_port22_bfin.txt
	@grep '^CHK' $(BUILD)/bench_port22_host.txt > $(BUILD)/chk_port22_host.txt
	@grep '^CHK' $(BUILD)/bench_port22_bfin.txt > $(BUILD)/chk_port22_bfin.txt
	@if cmp -s $(BUILD)/chk_port22_host.txt $(BUILD)/chk_port22_bfin.txt; then 	  echo "port, 22.05 kHz processors: host = Blackfin, $$(wc -l < $(BUILD)/chk_port22_host.txt) checksums identical"; 	else echo "port22: host and Blackfin DIFFER"; diff $(BUILD)/chk_port22_host.txt $(BUILD)/chk_port22_bfin.txt; exit 1; fi
	@grep '^CHK' $(BUILD)/bench_unmod_host.txt > $(BUILD)/chk_unmod_host.txt
	@grep '^CHK' $(BUILD)/bench_unmod_bfin.txt > $(BUILD)/chk_unmod_bfin.txt
	@grep '^CHK' $(BUILD)/bench_port_host.txt > $(BUILD)/chk_port_host.txt
	@grep '^CHK' $(BUILD)/bench_port_bfin.txt > $(BUILD)/chk_port_bfin.txt
	@if cmp -s $(BUILD)/chk_unmod_host.txt $(BUILD)/chk_unmod_bfin.txt; then \
	  echo "unmodified processors (31089 Hz): host = Blackfin, $$(wc -l < $(BUILD)/chk_unmod_host.txt) checksums identical"; \
	else echo "unmodified processors: host and Blackfin DIFFER"; diff $(BUILD)/chk_unmod_host.txt $(BUILD)/chk_unmod_bfin.txt; exit 1; fi
	@if cmp -s $(BUILD)/chk_port_host.txt $(BUILD)/chk_port_bfin.txt; then \
	  echo "port (44.1 kHz processors + core): host = Blackfin, $$(wc -l < $(BUILD)/chk_port_host.txt) checksums identical"; \
	else echo "port: host and Blackfin DIFFER"; diff $(BUILD)/chk_port_host.txt $(BUILD)/chk_port_bfin.txt; exit 1; fi

bench: $(BUILD)/bench_port_bfin.txt $(BUILD)/bench_port22_bfin.txt $(BUILD)/bench_unmod_bfin.txt
	@grep '^CYC core' $(BUILD)/bench_port_bfin.txt | sed 's/^CYC core/CYC core 44k/'
	@grep '^CYC core' $(BUILD)/bench_port22_bfin.txt | sed 's/^CYC core/CYC core 22k/'
	@grep '^CYC proc' $(BUILD)/bench_unmod_bfin.txt | sed 's/^CYC proc/CYC proc 31089 unmodified/'
	@grep '^CYC proc' $(BUILD)/bench_port_bfin.txt | sed 's/^CYC proc/CYC proc 44100 port/'

# --- OP-1 entry points --------------------------------------------------------
FX_SRC := op1/streams_fx.cc
FX_DEPS := tests/sim_fx.cc $(FX_SRC) op1/streams_fx.h $(HARNESS_HDR) $(DSP_SRC) $(DSP_HDR) $(CORE_SRC) $(CORE_HDR)
PROFILE ?= 44
ifeq ($(PROFILE),22)
CFG_FX := $(CFG_PORT22)
else
CFG_FX := $(CFG_PORT)
endif

$(BUILD)/sim_fx_host$(PROFILE): $(FX_DEPS) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(CFG_FX) -DOP1_SIM tests/sim_fx.cc $(FX_SRC) $(DSP_SRC) $(CORE_SRC) -o $@
$(BUILD)/sim_fx$(PROFILE).elf: $(FX_DEPS) | $(BUILD)
	$(BFIN_CXX) $(BFIN_CXXFLAGS) $(CFG_FX) -DOP1_SIM tests/sim_fx.cc $(FX_SRC) $(DSP_SRC) $(CORE_SRC) -o $@

sim-fx: $(BUILD)/sim_fx_host$(PROFILE) $(BUILD)/sim_fx$(PROFILE).elf
	$(BUILD)/sim_fx_host$(PROFILE) > $(BUILD)/sim_fx_host$(PROFILE).txt
	$(BFIN_RUN) $(BUILD)/sim_fx$(PROFILE).elf > $(BUILD)/sim_fx_bfin$(PROFILE).txt
	@cat $(BUILD)/sim_fx_bfin$(PROFILE).txt
	@grep '^CHK' $(BUILD)/sim_fx_host$(PROFILE).txt > $(BUILD)/sim_fx_chk_host.txt
	@grep '^CHK' $(BUILD)/sim_fx_bfin$(PROFILE).txt > $(BUILD)/sim_fx_chk_bfin.txt
	@if cmp -s $(BUILD)/sim_fx_chk_host.txt $(BUILD)/sim_fx_chk_bfin.txt; then \
	  echo "sim-fx (profile $(PROFILE)): host = Blackfin, $$(wc -l < $(BUILD)/sim_fx_chk_host.txt) checks identical"; \
	else echo "sim-fx: host and Blackfin DIFFER"; diff $(BUILD)/sim_fx_chk_host.txt $(BUILD)/sim_fx_chk_bfin.txt; exit 1; fi

# Freestanding link as the OP-1 image would be (no package): size, and no
# static constructors. Linked at 0x02c00000 with Rings' script and memset.
OP1_CXXFLAGS := -O2 -std=c++14 -mcpu=bf524 -fno-exceptions -fno-rtti -ffreestanding \
                -fno-threadsafe-statics -ffunction-sections -fdata-sections -I$(MI) -I. $(WARN)
$(BUILD)/op1_streams_fx.elf: $(FX_SRC) op1/streams_fx.h $(DSP_SRC) $(DSP_HDR) $(CORE_SRC) $(CORE_HDR) | $(BUILD)
	$(BFIN_CXX) $(OP1_CXXFLAGS) $(CFG_FX) $(FX_SRC) $(DSP_SRC) $(CORE_SRC) $(RINGS)/op1/support.cc \
	  -nostdlib -nostartfiles -T $(RINGS)/op1/link.ld -Wl,--gc-sections -Wl,-e,_StreamsFxMono \
	  -Wl,--undefined=_StreamsFxStereo -Wl,--undefined=_StreamsFxInit -o $@ -lgcc

op1-size: $(BUILD)/op1_streams_fx.elf
	$(BFIN_SIZE) -A $< | grep -E "^\.(text|rodata|data|bss|init_array)|Total"
	@if $(BFIN_OBJDUMP) -h $< | grep -E "init_array|ctors" | awk '{ if (strtonum("0x" $$3) != 0) exit 1 }'; then \
	  echo "no static constructors (.init_array / .ctors empty)"; else echo "STATIC CONSTRUCTORS PRESENT"; exit 1; fi

test: copy-check bench-check sim-fx

clean:
	rm -rf $(BUILD)/*.elf $(BUILD)/copy_check* $(BUILD)/render* $(BUILD)/bench* $(BUILD)/sim_fx*
