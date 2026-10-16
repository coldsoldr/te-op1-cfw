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
OP1_ENGINE_SRC := op1/engine.cc op1/screen.cc op1/input.cc op1/patches.cc op1/support.cc \
                  src/op1_voice.cc src/string_voice.cc src/string_fixed.cc src/reverb_fixed.cc \
                  src/fm_voice_fixed.cc src/modal_resonator.cc src/fixed_tables.cc \
                  src/mode_bank_bfin.S src/string_kernel_bfin.S src/limiter_bfin.S src/reverb_bfin.S
OP1_HDR := $(wildcard op1/*.h)

.PHONY: op1-probe op1-engine sim-engine sim-engine-fm host-probe step-params test-fm test-screen test-alloc \
        test-string sim-model bench-string test-reverb test-reverb-bus bench-mode-bank

$(BUILD)/op1_probe.elf: op1/probe.cc op1/support.cc op1/op1.h op1/link.ld | $(BUILD)
	$(BFIN_CXX) $(OP1_CXXFLAGS) op1/probe.cc op1/support.cc $(OP1_LDFLAGS) -o $@ -lgcc
	$(BFIN_BIN)/bfin-elf-size -A $@

$(BUILD)/op1_engine.elf: $(OP1_ENGINE_SRC) $(OP1_HDR) op1/link.ld $(PORT_HDR) | $(BUILD)
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
SIM_ENGINE_SRC := tests/sim_engine.cc op1/engine.cc op1/screen.cc op1/input.cc op1/patches.cc \
                  src/op1_voice.cc src/string_voice.cc src/string_fixed.cc src/reverb_fixed.cc \
                  src/fm_voice_fixed.cc src/modal_resonator.cc src/fixed_tables.cc \
                  src/mode_bank_bfin.S src/string_kernel_bfin.S src/limiter_bfin.S src/reverb_bfin.S

$(BUILD)/sim_engine.elf: $(SIM_ENGINE_SRC) $(OP1_HDR) $(PORT_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) $(SIM_ENGINE_SRC) -o $@

sim-engine: $(BUILD)/sim_engine.elf
	$(BFIN_RUN) $< $(BUILD)/sim_engine.raw

#   make sim-engine-fm  the same with the FM voice -> build/sim_engine_fm.raw
$(BUILD)/sim_engine_fm.elf: $(SIM_ENGINE_SRC) $(OP1_HDR) $(PORT_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) -DSIM_MODEL=3 $(SIM_ENGINE_SRC) -o $@

sim-engine-fm: $(BUILD)/sim_engine_fm.elf
	$(BFIN_RUN) $< $(BUILD)/sim_engine_fm.raw

#   make sim-model M=n  the same with model n (op1rings::ResonatorModel)
#                       -> build/sim_engine_model<n>.raw
$(BUILD)/sim_engine_model%.elf: $(SIM_ENGINE_SRC) $(OP1_HDR) $(PORT_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) -DSIM_MODEL=$* $(SIM_ENGINE_SRC) -o $@

sim-model: $(BUILD)/sim_engine_model$(M).elf
	$(BFIN_RUN) $< $(BUILD)/sim_engine_model$(M).raw

$(BUILD)/host_probe: tests/host_probe.cc op1/probe.cc $(OP1_HDR) | $(BUILD)
	$(HOST_CXX) -O2 -std=c++17 -DOP1_HOST_SIM -DOP1_SIM -I. $(WARN) tests/host_probe.cc op1/probe.cc -o $@

#   make step-params  clicks from parameter steps, upstream vs port, and a
#                     check that a sustained note has no discontinuities
STEP_PARAMS_SRC := tests/step_params.cc src/modal_resonator.cc src/op1_voice.cc \
                   src/string_voice.cc src/string_fixed.cc src/reverb_fixed.cc \
                   src/fm_voice_fixed.cc src/fixed_tables.cc $(MI)/rings/dsp/resonator.cc \
                   $(MI)/rings/resources.cc $(MI)/stmlib/dsp/units.cc

$(BUILD)/step_params: $(STEP_PARAMS_SRC) $(OP1_HDR) $(PORT_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(STEP_PARAMS_SRC) -o $@ -lm

step-params: $(BUILD)/step_params
	$<

#   make test-alloc   op1/voice_allocator.h (polyphony) and op1/settings.h
$(BUILD)/test_voice_allocator: tests/test_voice_allocator.cc $(OP1_HDR) | $(BUILD)
	$(HOST_CXX) -O2 -std=c++17 -I. $(WARN) tests/test_voice_allocator.cc -o $@

test-alloc: $(BUILD)/test_voice_allocator
	$<

#   make test-screen  op1/screen_math.h against the screen spec's formulas
$(BUILD)/test_screen_math: tests/test_screen_math.cc $(OP1_HDR) | $(BUILD)
	$(HOST_CXX) -O2 -std=c++17 -I. $(WARN) tests/test_screen_math.cc -o $@ -lm

test-screen: $(BUILD)/test_screen_math
	$<

#   rings.svg for the package: the screen spec's SVG (incoming/rings.svg) with
#   the ids op1/screen.cc needs (op1/gen_rings_svg.py)
op1/rings.svg: op1/gen_rings_svg.py ../../incoming/rings.svg
	$(PYTHON) op1/gen_rings_svg.py ../../incoming/rings.svg $@

#   make test-fm      the fixed-point FM voice against upstream's float FMVoice
TEST_FM_SRC := tests/test_fm_voice.cc src/fm_voice_fixed.cc src/fixed_tables.cc \
               $(MI)/rings/dsp/fm_voice.cc $(MI)/rings/resources.cc \
               $(MI)/stmlib/dsp/units.cc

$(BUILD)/test_fm_voice: $(TEST_FM_SRC) $(PORT_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DOP1RINGS_SAMPLE_RATE=48000 $(TEST_FM_SRC) -o $@ -lm

test-fm: $(BUILD)/test_fm_voice
	$<

#   make test-string  the fixed-point string models against upstream's float
#                     String and Plucker
TEST_STRING_SRC := tests/test_string.cc src/string_voice.cc src/string_fixed.cc \
                   src/fixed_tables.cc $(REF_SRC)

$(BUILD)/test_string: $(TEST_STRING_SRC) $(PORT_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DOP1RINGS_SAMPLE_RATE=48000 $(TEST_STRING_SRC) -o $@ -lm

test-string: $(BUILD)/test_string
	$<

#   make bench-string  cycles of the fixed-point string on Blackfin
BENCH_STRING_SRC := tests/bench_string.cc src/string_fixed.cc src/fixed_tables.cc src/string_kernel_bfin.S src/reverb_fixed.cc src/reverb_bfin.S

$(BUILD)/bench_string.elf: $(BENCH_STRING_SRC) $(PORT_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) $(BENCH_STRING_SRC) -o $@

#   The host build runs the reverb's C loop; its "reverb:" line (samples,
#   guards, checksum) must match the Blackfin one, which runs the assembly.
$(BUILD)/bench_string_host: $(filter-out %.S,$(BENCH_STRING_SRC)) $(PORT_HDR) | $(BUILD)
	$(HOST_CXX) -O2 -std=c++17 -I. $(WARN) $(filter-out %.S,$(BENCH_STRING_SRC)) -o $@

bench-string: $(BUILD)/bench_string.elf $(BUILD)/bench_string_host
	@$(BFIN_RUN) $< > $(BUILD)/bench_string.txt; s=$$?; cat $(BUILD)/bench_string.txt; exit $$s
	@$(BUILD)/bench_string_host > $(BUILD)/bench_string_host.txt
	@grep '^reverb:' $(BUILD)/bench_string_host.txt | sed 's/^/host  /'
	@if [ "$$(grep '^reverb:' $(BUILD)/bench_string.txt)" = "$$(grep '^reverb:' $(BUILD)/bench_string_host.txt)" ]; \
	then echo "reverb: Blackfin = host"; else echo "reverb: Blackfin DIFFERS from host"; exit 1; fi

#   make bench-mode-bank  the modal mode bank: cycles on Blackfin, and the
#                         assembly against C (Blackfin and host builds, two
#                         outputs and one): every checksum must agree
$(BUILD)/bench_mode_bank.elf: tests/bench_mode_bank.cc src/mode_bank_bfin.S $(PORT_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) tests/bench_mode_bank.cc src/mode_bank_bfin.S -o $@

$(BUILD)/bench_mode_bank_host: tests/bench_mode_bank.cc $(PORT_HDR) | $(BUILD)
	$(HOST_CXX) -O2 -std=c++17 -I. $(WARN) tests/bench_mode_bank.cc -o $@ -lm

bench-mode-bank: $(BUILD)/bench_mode_bank.elf $(BUILD)/bench_mode_bank_host
	@$(BFIN_RUN) $< > $(BUILD)/bench_mode_bank.txt; s=$$?; cat $(BUILD)/bench_mode_bank.txt; exit $$s
	@$(BUILD)/bench_mode_bank_host | sed 's/^/host /' | tee $(BUILD)/bench_mode_bank_host.txt
	@two=$$(cat $(BUILD)/bench_mode_bank.txt $(BUILD)/bench_mode_bank_host.txt | grep checksum | grep -v mono | awk '{print $$NF}' | sort -u | wc -l); \
	one=$$(cat $(BUILD)/bench_mode_bank.txt $(BUILD)/bench_mode_bank_host.txt | grep checksum | grep mono | awk '{print $$NF}' | sort -u | wc -l); \
	if [ "$$two" = 1 ] && [ "$$one" = 1 ]; then echo "mode bank: assembly = C = host"; else echo "mode bank: checksums DIFFER"; exit 1; fi

#   make test-reverb  the fixed-point reverb against upstream's float Reverb
TEST_REVERB_SRC := tests/test_reverb.cc src/reverb_fixed.cc src/fixed_tables.cc $(MI)/stmlib/dsp/units.cc

$(BUILD)/test_reverb: $(TEST_REVERB_SRC) $(PORT_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DOP1RINGS_SAMPLE_RATE=48000 $(TEST_REVERB_SRC) -o $@ -lm

test-reverb: $(BUILD)/test_reverb
	$<

#   make test-reverb-bus  op1/reverb_bus.h: string + reverb's shared reverb
#                         (one run per OP-1 block, the voices' shares)
TEST_REVERB_BUS_SRC := tests/test_reverb_bus.cc src/op1_voice.cc src/string_voice.cc src/string_fixed.cc                        src/reverb_fixed.cc src/fm_voice_fixed.cc src/modal_resonator.cc src/fixed_tables.cc

$(BUILD)/test_reverb_bus: $(TEST_REVERB_BUS_SRC) $(OP1_HDR) $(PORT_HDR) | $(BUILD)
	$(HOST_CXX) -O2 -std=c++17 -I. $(WARN) $(TEST_REVERB_BUS_SRC) -o $@

test-reverb-bus: $(BUILD)/test_reverb_bus
	$<

HOST_PROBE_ARGS ?= 400 40 1 0
host-probe: $(BUILD)/host_probe
	$(BUILD)/host_probe $(BUILD)/host_probe.wav $(HOST_PROBE_ARGS)

clean:
	rm -rf $(BUILD) $(OUT)

#   make bench-models   cycles per voice and model, and of their parts
BENCH_MODELS_SRC := tests/bench_models.cc src/op1_voice.cc src/string_voice.cc src/string_fixed.cc \
                    src/reverb_fixed.cc src/fm_voice_fixed.cc src/modal_resonator.cc \
                    src/fixed_tables.cc src/mode_bank_bfin.S src/string_kernel_bfin.S src/limiter_bfin.S src/reverb_bfin.S

$(BUILD)/bench_models.elf: $(BENCH_MODELS_SRC) $(PORT_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) $(BENCH_MODELS_SRC) -o $@

bench-models: $(BUILD)/bench_models.elf
	$(BFIN_RUN) $<
