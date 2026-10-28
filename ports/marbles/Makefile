# Marbles port: upstream reference, copy check, fixed-point port, comparison
# and Blackfin benchmarks. Run inside WSL from this directory:
#   wsl -e bash -lc 'cd /mnt/<drive>/<repo>/ports/marbles && make ...'
#
#   make ref          upstream float Marbles at the port's rate  -> out/ref/*.bin
#   make copy         the copy in upstream/ (op1marbles)          -> out/copy/*.bin
#   make copy-check   text check of the copy + its outputs byte-identical to ref
#   make port         the fixed-point port                        -> out/port/*.bin
#   make test         component tests: port vs float copy (quantizer, beta, sequence, ...)
#   make compare      side-by-side comparison, port vs float copy (32 seeds x 300 s
#                     per configuration) -> out/compare.txt
#   make bench        port on Blackfin (bfin-elf-run): checksums == host, cycles per tick
#   make bench-ref    float copy on Blackfin (soft-float), for scale
#   make all          everything above except the benches
#
# SUBSTEPS=n (default 1): Marbles samples per OP-1 LFO call (689.0625 calls/s).
# Run `make clean` when changing it (the targets don't depend on it).

MI        ?= ../../mutableinstruments
BFIN_BIN  ?= $(HOME)/op1-toolchain/bfin/bin
PYTHON    ?= python3
BUILD     := build
OUT       := out

WARN := -Wall -Wno-unused-variable -Wno-unused-local-typedefs -Wno-unused-function

STMLIB_SRC := $(MI)/stmlib/dsp/units.cc
REF_SRC    := $(MI)/marbles/random/t_generator.cc $(MI)/marbles/random/x_y_generator.cc \
              $(MI)/marbles/random/output_channel.cc $(MI)/marbles/random/lag_processor.cc \
              $(MI)/marbles/random/quantizer.cc $(MI)/marbles/ramp/ramp_extractor.cc \
              $(MI)/marbles/resources.cc $(STMLIB_SRC)
COPY_SRC   := upstream/random/t_generator.cc upstream/random/x_y_generator.cc \
              upstream/random/output_channel.cc upstream/random/lag_processor.cc \
              upstream/random/quantizer.cc upstream/ramp/ramp_extractor.cc \
              upstream/resources.cc $(STMLIB_SRC)
COPY_HDR   := $(wildcard upstream/*/*.h upstream/*.h upstream/*.inc)
PORT_SRC   := src/t_generator_fx.cc src/x_y_generator_fx.cc src/output_channel_fx.cc \
              src/quantizer_fx.cc src/tables_fx.cc
PORT_HDR   := $(wildcard src/*.h)
HARNESS    := $(wildcard harness/*.h)

HOST_CXX      := g++
SUBSTEPS      ?= 1
HOST_CXXFLAGS := -O2 -g -std=c++11 -DTEST -DOP1MARBLES_SUBSTEPS=$(SUBSTEPS) -I. -I$(MI) $(WARN)
BFIN_CXX      := $(BFIN_BIN)/bfin-elf-g++
BFIN_RUN      := $(BFIN_BIN)/bfin-elf-run
BFIN_CXXFLAGS := -O2 -std=c++11 -mcpu=bf524 -msim -fno-exceptions -fno-rtti -DTEST -DOP1_SIM \
                 -DOP1MARBLES_SUBSTEPS=$(SUBSTEPS) -I. -I$(MI) $(WARN)
BENCH_SECONDS ?= 4

.PHONY: all ref copy copy-check port test compare bench bench-ref tables sim-seq clean

all: copy-check port test compare

$(BUILD) $(OUT)/ref $(OUT)/copy $(OUT)/port:
	mkdir -p $@

$(BUILD)/render_ref: harness/render.cc $(HARNESS) $(REF_SRC) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DIMPL_REF harness/render.cc $(REF_SRC) -o $@ -lm

$(BUILD)/render_copy: harness/render.cc $(HARNESS) $(COPY_SRC) $(COPY_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DIMPL_COPY harness/render.cc $(COPY_SRC) -o $@ -lm

$(BUILD)/render_port: harness/render.cc $(HARNESS) $(PORT_SRC) $(PORT_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -DIMPL_PORT harness/render.cc $(PORT_SRC) -o $@

ref: $(BUILD)/render_ref | $(OUT)/ref
	$(BUILD)/render_ref $(OUT)/ref | tee $(OUT)/ref.txt

copy: $(BUILD)/render_copy | $(OUT)/copy
	$(BUILD)/render_copy $(OUT)/copy | tee $(OUT)/copy.txt

# The copy must be upstream with only the renames (text check), and must
# render byte-identical outputs.
copy-check: ref copy
	$(PYTHON) scripts/copy_upstream.py --check
	@for f in $(OUT)/ref/*.bin; do cmp $$f $(OUT)/copy/$$(basename $$f) || exit 1; done
	@echo "copy-check: outputs of all scenarios byte-identical (ref == copy)"

port: $(BUILD)/render_port | $(OUT)/port
	$(BUILD)/render_port $(OUT)/port | tee $(OUT)/port.txt

# Fixed-point tables generated from upstream's float tables.
tables: scripts/gen_tables.py
	$(PYTHON) scripts/gen_tables.py $(MI) src/tables_fx.cc

$(BUILD)/test_components: tests/test_components.cc $(HARNESS) $(PORT_SRC) $(PORT_HDR) $(COPY_SRC) $(COPY_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) tests/test_components.cc $(PORT_SRC) $(COPY_SRC) -o $@ -lm

test: $(BUILD)/test_components
	$(BUILD)/test_components

$(BUILD)/compare: tests/compare.cc $(HARNESS) $(PORT_SRC) $(PORT_HDR) $(COPY_SRC) $(COPY_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) tests/compare.cc $(PORT_SRC) $(COPY_SRC) -o $@ -lm

compare: $(BUILD)/compare
	$(BUILD)/compare | tee $(OUT)/compare.txt

$(BUILD)/render_port_bfin.elf: harness/render.cc $(HARNESS) $(PORT_SRC) $(PORT_HDR) | $(BUILD)
	$(BFIN_CXX) $(BFIN_CXXFLAGS) -DIMPL_PORT harness/render.cc $(PORT_SRC) -o $@

$(BUILD)/render_copy_bfin.elf: harness/render.cc $(HARNESS) $(COPY_SRC) $(COPY_HDR) | $(BUILD)
	$(BFIN_CXX) $(BFIN_CXXFLAGS) -DIMPL_COPY harness/render.cc $(COPY_SRC) -o $@ -lm

# Checksums must equal the host's for the same length: compare with
# `build/render_port - $(BENCH_SECONDS)`.
bench: $(BUILD)/render_port_bfin.elf $(BUILD)/render_port
	$(BUILD)/render_port "" $(BENCH_SECONDS) > $(BUILD)/bench_host.txt
	$(BFIN_RUN) $< "" $(BENCH_SECONDS) | tee $(BUILD)/bench_bfin.txt
	$(PYTHON) scripts/check_bench.py $(BUILD)/bench_host.txt $(BUILD)/bench_bfin.txt

bench-ref: $(BUILD)/render_copy_bfin.elf
	$(BFIN_RUN) $< "" 2

clean:
	rm -rf $(BUILD) $(OUT)

# make sim-seq: the sequencer (op1/marbles_seq.*) driven as the OP-1 drives a
# sequencer, on the host and in the Blackfin simulator (cycles per tick).
SEQ_SRC := tests/sim_seq.cc op1/marbles_seq.cc $(PORT_SRC)
$(BUILD)/sim_seq: $(SEQ_SRC) $(PORT_HDR) op1/marbles_seq.h | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(SEQ_SRC) -o $@
$(BUILD)/sim_seq.elf: $(SEQ_SRC) $(PORT_HDR) op1/marbles_seq.h | $(BUILD)
	$(BFIN_CXX) $(BFIN_CXXFLAGS) $(SEQ_SRC) -o $@
sim-seq: $(BUILD)/sim_seq $(BUILD)/sim_seq.elf
	$(BUILD)/sim_seq
	$(BFIN_RUN) $(BUILD)/sim_seq.elf

# marbles.svg for the package: the redesigned screen
# (docs/module-briefs/designs/marbles.svg, drawn in the device subset) checked
# by the fx screens' generator for the ids op1/marbles_op1.cc needs (eng0/eng1
# are static, checked as well). The old op1/gen_marbles_svg.py (with its
# idP2.. subpath split) is no longer used.
empty :=
space := $(empty) $(empty)
comma := ,
MARBLES_MARKS   := $(foreach l,0 1 2,$(foreach c,0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15,h$(l)$(c)))
MARBLES_DESIGN  := ../../docs/module-briefs/designs/marbles.svg
MARBLES_SVG_IDS := eng0,eng1,page1ind,page2ind,src_scale,src_keys,chipT,chipX,chip_model,chip_scale,loop_t,loopX,bar0,bar1,bar2,bar3,leg0_t,leg1_t,leg2_t,leg0X,leg1X,leg2X,$(subst $(space),$(comma),$(MARBLES_MARKS))
op1/marbles.svg: ../fx/op1/gen_fx_svg.py $(MARBLES_DESIGN)
	$(PYTHON) ../fx/op1/gen_fx_svg.py $(MARBLES_DESIGN) $@ --ids $(MARBLES_SVG_IDS)
