# Plaits port: upstream reference renders, the bit-identical copy check, the
# fixed-point port's host and Blackfin builds, comparison and benchmarks. Run
# from this directory inside WSL (g++; bfin-elf-g++ / bfin-elf-run from
# ~/op1-toolchain/bfin/bin for the Blackfin targets).
#
#   make ref          upstream float Plaits, 48 kHz, per engine  -> out/ref/*.wav
#   make copy         the copy in src/ (namespace op1plaits)     -> out/copy/*.wav
#   make copy-check   out/copy = out/ref, byte for byte
#   make port         the fixed-point port at 48 kHz, 12-sample blocks (comparable
#                     with ref; converted engines only)          -> out/port/*.wav
#   make compare      SNR (dirdiff) and band spectra (dirspec) of out/port vs out/ref
#   make bench        the port on Blackfin at the OP-1 rate: checksums must equal
#                     the host build's; simulated cycles per 128-sample OP-1 call
#   make test-fixed   fixed-point helpers (reciprocal, tables) vs float, host = Blackfin
#   make test-voice   the OP-1 voice wrapper (op1/) in the simulator

MI        ?= ../../mutableinstruments
BFIN_BIN  ?= $(HOME)/op1-toolchain/bfin/bin
PYTHON    ?= python3
BUILD     := build
OUT       := out
COMPARE   := ../rings/harness/compare.py

WARN := -Wall -Wno-unused-variable -Wno-unused-local-typedefs -Wno-unused-function \
        -Wno-unused-but-set-variable

STMLIB_SRC := $(MI)/stmlib/dsp/units.cc $(MI)/stmlib/utils/random.cc

ENGINE_NAMES := virtual_analog_engine waveshaping_engine fm_engine grain_engine \
                additive_engine wavetable_engine chord_engine speech_engine swarm_engine \
                noise_engine particle_engine string_engine modal_engine bass_drum_engine \
                snare_drum_engine hi_hat_engine
DSP_REL := $(addprefix engine/,$(addsuffix .cc,$(ENGINE_NAMES))) \
           chords/chord_bank.cc physical_modelling/modal_voice.cc \
           physical_modelling/resonator.cc physical_modelling/string.cc \
           physical_modelling/string_voice.cc speech/lpc_speech_synth.cc \
           speech/lpc_speech_synth_controller.cc speech/lpc_speech_synth_phonemes.cc \
           speech/lpc_speech_synth_words.cc speech/naive_speech_synth.cc \
           speech/sam_speech_synth.cc

REF_SRC  := $(addprefix $(MI)/plaits/dsp/,$(DSP_REL)) $(MI)/plaits/resources.cc $(STMLIB_SRC)
COPY_SRC := $(addprefix src/,$(DSP_REL)) src/resources.cc $(STMLIB_SRC)
COPY_HDR := $(shell find src -name '*.h')

FIXED_SRC := $(wildcard fixed/*.cc)
FIXED_HDR := $(wildcard fixed/*.h)
HARNESS   := harness/render.cc harness/scenarios.h

HOST_CXX      := g++
HOST_CXXFLAGS := -O2 -g -std=c++14 -fno-lifetime-dse -DTEST -I$(MI) -I. -I../rings $(WARN)

BFIN_CXX  := $(BFIN_BIN)/bfin-elf-g++
BFIN_RUN  := $(BFIN_BIN)/bfin-elf-run
SIM_CXXFLAGS := -O2 -std=c++14 -mcpu=bf524 -msim -fno-exceptions -fno-rtti \
                -DOP1_SIM -I. -I../rings $(WARN)

# The OP-1 configuration: 44.1 kHz internal rate, 64 samples per OP-1 call
# (upsampled 2x to 128 at 88.2 kHz), split into PLAITS_BLOCK_SIZE blocks.
OP1_DEFS     := -DPLAITS_SAMPLE_RATE=44100 -DPLAITS_BLOCK_SIZE=16
COMPARE_DEFS := -DPLAITS_SAMPLE_RATE=48000 -DPLAITS_BLOCK_SIZE=12

.PHONY: all ref copy copy-check port compare compare-spec bench test-fixed test-voice clean

all: copy-check port compare

$(BUILD) $(OUT)/ref $(OUT)/copy $(OUT)/port:
	mkdir -p $@

$(BUILD)/render_ref: $(HARNESS) $(REF_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DREF harness/render.cc $(REF_SRC) -o $@ -lm

$(BUILD)/render_copy: $(HARNESS) $(COPY_SRC) $(COPY_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DCOPY harness/render.cc $(COPY_SRC) -o $@ -lm

$(BUILD)/render_port: $(HARNESS) $(FIXED_SRC) $(FIXED_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(COMPARE_DEFS) -DPORT harness/render.cc $(FIXED_SRC) -o $@ -lm

ref: $(BUILD)/render_ref | $(OUT)/ref
	$(BUILD)/render_ref $(OUT)/ref

copy: $(BUILD)/render_copy | $(OUT)/copy
	$(BUILD)/render_copy $(OUT)/copy

copy-check: ref copy
	@n=0; bad=0; for f in $(OUT)/ref/*.wav; do b=$$(basename $$f); n=$$((n+1)); \
	  cmp -s $$f $(OUT)/copy/$$b || { echo "DIFFERS: $$b"; bad=$$((bad+1)); }; done; \
	  if [ $$bad = 0 ]; then echo "copy-check: $$n files, copy = upstream bit for bit"; \
	  else echo "copy-check: $$bad of $$n files differ"; exit 1; fi

port: $(BUILD)/render_port | $(OUT)/port
	rm -f $(OUT)/port/*.wav
	$(BUILD)/render_port $(OUT)/port

compare:
	$(PYTHON) $(COMPARE) dirdiff $(OUT)/ref $(OUT)/port

compare-spec:
	$(PYTHON) $(COMPARE) dirspec $(OUT)/ref $(OUT)/port

#   make bench [E=n] [S=seconds]   checksums host = Blackfin, cycles per call
BENCH_SRC := tests/bench_engines.cc $(FIXED_SRC) $(wildcard fixed/*.S)
E ?= -1
S ?= 0.5

$(BUILD)/bench_engines.elf: $(BENCH_SRC) $(FIXED_HDR) harness/scenarios.h | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) $(OP1_DEFS) $(BENCH_SRC) -o $@

$(BUILD)/bench_engines_host: $(filter-out %.S,$(BENCH_SRC)) $(FIXED_HDR) harness/scenarios.h | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(OP1_DEFS) $(filter-out %.S,$(BENCH_SRC)) -o $@

bench: $(BUILD)/bench_engines.elf $(BUILD)/bench_engines_host
	@$(BFIN_RUN) $(BUILD)/bench_engines.elf $(E) $(S) > $(BUILD)/bench_bfin.txt; s=$$?; cat $(BUILD)/bench_bfin.txt; exit $$s
	@$(BUILD)/bench_engines_host $(E) $(S) > $(BUILD)/bench_host.txt
	@if [ "$$(grep '^checksum' $(BUILD)/bench_bfin.txt)" = "$$(grep '^checksum' $(BUILD)/bench_host.txt)" ]; \
	then echo "bench: Blackfin checksums = host ($$(grep -c '^checksum' $(BUILD)/bench_host.txt) runs)"; \
	else echo "bench: Blackfin checksums DIFFER from host"; grep '^checksum' $(BUILD)/bench_bfin.txt > $(BUILD)/bench_bfin.sum; \
	  grep '^checksum' $(BUILD)/bench_host.txt > $(BUILD)/bench_host.sum; \
	  diff $(BUILD)/bench_bfin.sum $(BUILD)/bench_host.sum; exit 1; fi

#   make tables   regenerate fixed/tables.{h,cc} from upstream's float tables
$(BUILD)/gen_tables: resources/gen_tables.cc $(MI)/plaits/resources.cc | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) resources/gen_tables.cc $(MI)/plaits/resources.cc \
	  $(MI)/plaits/dsp/speech/lpc_speech_synth_words.cc $(MI)/plaits/dsp/speech/lpc_speech_synth_phonemes.cc \
	  $(MI)/plaits/dsp/speech/naive_speech_synth.cc $(MI)/plaits/dsp/speech/sam_speech_synth.cc \
	  $(MI)/plaits/dsp/speech/lpc_speech_synth.cc $(MI)/plaits/dsp/speech/lpc_speech_synth_controller.cc \
	  $(MI)/stmlib/dsp/units.cc $(MI)/stmlib/utils/random.cc -o $@ -lm

tables: $(BUILD)/gen_tables
	$(BUILD)/gen_tables fixed/tables.h fixed/tables.cc

clean:
	rm -rf $(BUILD) $(OUT)

#   make test-oscillators   fixed oscillators vs upstream's float ones
$(BUILD)/test_oscillators: tests/test_oscillators.cc $(FIXED_HDR) $(FIXED_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(COMPARE_DEFS) tests/test_oscillators.cc fixed/tables.cc fixed/engines.cc \
	  $(filter-out fixed/engines.cc fixed/tables.cc,$(FIXED_SRC)) $(MI)/stmlib/dsp/units.cc -o $@ -lm

test-oscillators: $(BUILD)/test_oscillators
	$<

#   make probe   build/probe_engine: one engine, upstream vs port, constant parameters
$(BUILD)/probe_engine: tests/probe_engine.cc $(REF_SRC) $(FIXED_HDR) $(FIXED_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(COMPARE_DEFS) tests/probe_engine.cc $(REF_SRC) $(FIXED_SRC) -o $@ -lm

probe: $(BUILD)/probe_engine

#   make ref64   upstream compiled with float -> double (-Dfloat=double): a
#                higher-precision reference, to tell upstream's float rounding
#                (its float phase accumulators drift by ~1e-5) from port errors
$(BUILD)/render_ref64: $(HARNESS) $(REF_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -include harness/ref64_prelude.h -DREF harness/render.cc $(REF_SRC) -o $@ -lm

$(OUT)/ref64:
	mkdir -p $@

ref64: $(BUILD)/render_ref64 | $(OUT)/ref64
	$(BUILD)/render_ref64 $(OUT)/ref64 $(E64)

#   make check-calls   library calls made from the port's code on Blackfin: no
#                      soft-float (___*df3, ___*sf3, ___fix*, ___float*) and no
#                      divisions (___div*, ___udiv*, ___mod*) may appear; 64-bit
#                      multiplies (___muldi3) are listed (control rate only)
check-calls: $(BUILD)/bench_engines.elf
	@$(BFIN_BIN)/bfin-elf-objdump -d $< | awk '/^[0-9a-f]+ <__ZN9op1plaits|^[0-9a-f]+ <__ZN8op1rings/ {f=$$2} /^$$/ {f=""} f && /CALL 0x/ {print f, $$NF}' \
	  | sed 's/[<>;:]//g' | sort | uniq -c | sort -rn > $(BUILD)/calls.txt
	@cat $(BUILD)/calls.txt | grep -v "__ZN" || true
	@if grep -q "df3\|sf3\|___fix\|___float\|___div\|___udiv\|___mod\|___umod" $(BUILD)/calls.txt; \
	then echo "check-calls: soft-float or division calls FOUND"; exit 1; else echo "check-calls: no soft-float, no divisions"; fi

#   make test-filters   fixed SVF / one-pole vs stmlib's float filters
$(BUILD)/test_filters: tests/test_filters.cc fixed/svf.h fixed/fixed_math.h fixed/tables.cc | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(COMPARE_DEFS) tests/test_filters.cc fixed/tables.cc \
	  fixed/engines.cc $(filter-out fixed/engines.cc fixed/tables.cc,$(FIXED_SRC)) -o $@ -lm

test-filters: $(BUILD)/test_filters
	$<

#   make test-fixed   primitives: MulRound (inline asm) = reference on Blackfin,
#                     and checksums of the helpers, Blackfin = host
$(BUILD)/test_fixed.elf: tests/test_fixed.cc $(FIXED_HDR) fixed/tables.cc | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) $(OP1_DEFS) tests/test_fixed.cc fixed/tables.cc -o $@

$(BUILD)/test_fixed_host: tests/test_fixed.cc $(FIXED_HDR) fixed/tables.cc | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(OP1_DEFS) tests/test_fixed.cc fixed/tables.cc -o $@

test-fixed: $(BUILD)/test_fixed.elf $(BUILD)/test_fixed_host
	@$(BFIN_RUN) $(BUILD)/test_fixed.elf > $(BUILD)/test_fixed_bfin.txt; cat $(BUILD)/test_fixed_bfin.txt
	@$(BUILD)/test_fixed_host > $(BUILD)/test_fixed_host.txt
	@if diff -q $(BUILD)/test_fixed_bfin.txt $(BUILD)/test_fixed_host.txt >/dev/null; \
	then echo "test-fixed: Blackfin = host"; else echo "test-fixed: Blackfin DIFFERS from host"; \
	diff $(BUILD)/test_fixed_bfin.txt $(BUILD)/test_fixed_host.txt; exit 1; fi

#   make voice-compare   the whole voice (trigger, decay envelope, LPG, post-
#                        processing, int16 output): the copy's Voice vs VoiceFixed
$(OUT)/voice_ref $(OUT)/voice_port:
	mkdir -p $@

$(BUILD)/render_voice_copy: harness/render_voice.cc harness/scenarios.h $(COPY_SRC) $(COPY_HDR) src/voice.cc | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DCOPY harness/render_voice.cc src/voice.cc $(COPY_SRC) -o $@ -lm

$(BUILD)/render_voice_port: harness/render_voice.cc harness/scenarios.h $(FIXED_SRC) $(FIXED_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(COMPARE_DEFS) -DPORT harness/render_voice.cc $(FIXED_SRC) -o $@ -lm

voice-compare: $(BUILD)/render_voice_copy $(BUILD)/render_voice_port | $(OUT)/voice_ref $(OUT)/voice_port
	$(BUILD)/render_voice_copy $(OUT)/voice_ref
	rm -f $(OUT)/voice_port/*.wav
	$(BUILD)/render_voice_port $(OUT)/voice_port
	$(PYTHON) $(COMPARE) dirdiff $(OUT)/voice_ref $(OUT)/voice_port

#   make test-voice [E=n]   the OP-1 glue (op1/) in the simulator and on the
#                           host: checksums equal, A3 pitch, cycles per call
OP1_GLUE_SRC := op1/plaits_op1.cc $(FIXED_SRC)

$(BUILD)/sim_voice.elf: tests/sim_voice.cc $(OP1_GLUE_SRC) $(FIXED_HDR) op1/plaits_op1.h | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) $(OP1_DEFS) tests/sim_voice.cc $(OP1_GLUE_SRC) -o $@

$(BUILD)/sim_voice_host: tests/sim_voice.cc $(OP1_GLUE_SRC) $(FIXED_HDR) op1/plaits_op1.h | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DOP1_SIM -DOP1_HOST_SIM $(OP1_DEFS) tests/sim_voice.cc $(OP1_GLUE_SRC) -o $@ \
	  -Wl,--defsym=_ZN3op110host_cyclesE=0

test-voice: $(BUILD)/sim_voice.elf $(BUILD)/sim_voice_host
	@$(BFIN_RUN) $(BUILD)/sim_voice.elf $(E) $(BUILD)/sim_voice.raw > $(BUILD)/sim_voice_bfin.txt; s=$$?; cat $(BUILD)/sim_voice_bfin.txt; exit $$s
	@$(BUILD)/sim_voice_host $(E) > $(BUILD)/sim_voice_host.txt
	@if [ "$$(grep '^checksum' $(BUILD)/sim_voice_bfin.txt)" = "$$(grep '^checksum' $(BUILD)/sim_voice_host.txt)" ]; \
	then echo "test-voice: Blackfin = host"; else echo "test-voice: Blackfin DIFFERS from host"; exit 1; fi

#   make image   the glue linked as a freestanding OP-1 image (op1/image_check.cc,
#                ports/rings/op1/link.ld): sizes, empty .init_array, no soft-float
OP1_CXXFLAGS := -O2 -std=c++14 -mcpu=bf524 -ffreestanding -fno-exceptions -fno-rtti \
                -fno-threadsafe-statics -fno-use-cxa-atexit -ffunction-sections -fdata-sections \
                -I. -I../rings $(WARN) $(OP1_DEFS)
OP1_LDFLAGS  := -nostdlib -nostartfiles -T ../rings/op1/link.ld -Wl,--gc-sections -Wl,--no-undefined

$(BUILD)/plaits_image.elf: op1/image_check.cc $(OP1_GLUE_SRC) $(FIXED_HDR) op1/plaits_op1.h | $(BUILD)
	$(BFIN_CXX) $(OP1_CXXFLAGS) op1/image_check.cc $(OP1_GLUE_SRC) ../rings/op1/support.cc \
	  $(OP1_LDFLAGS) -o $@ -lgcc

image: $(BUILD)/plaits_image.elf
	@$(BFIN_BIN)/bfin-elf-size -A $< | grep -v "^\.debug\|^$$"
	@init=$$($(BFIN_BIN)/bfin-elf-size -A $< | awk '/^\.init_array/ {print $$2}'); \
	  if [ "$$init" = "0" ] || [ -z "$$init" ]; then echo "image: no run-time initialisers"; \
	  else echo "image: .init_array is $$init bytes"; exit 1; fi
	@if $(BFIN_BIN)/bfin-elf-nm $< | grep -q "df3\|sf3\|___fix\|___float\|___div\|___udiv\|___mod\|___umod"; \
	  then echo "image: soft-float / division helpers linked:"; $(BFIN_BIN)/bfin-elf-nm $< | grep "df3\|sf3\|___fix\|___float\|___div\|___udiv\|___mod\|___umod"; exit 1; \
	  else echo "image: no soft-float, no division helpers"; fi
	@$(BFIN_BIN)/bfin-elf-nm $< | grep " [Tt] ___" | sed 's/^/image: libgcc /'
