# Grids port: bit-exact comparison with upstream and Blackfin benchmark.
# Run inside WSL from this directory:
#   wsl -e bash -lc 'cd /mnt/<drive>/<repo>/ports/grids && make ...'
#
#   make compare    port vs upstream pattern_generator.cc (host, shims in shim/),
#                   plus the OP-1 layer checks             -> out/compare.txt
#   make patterns   compare + ASCII patterns for a few map points -> out/patterns.txt
#   make sim        the port on the Blackfin simulator (bfin-elf-run): checksum
#                   must equal the host's; cycles per tick  -> out/sim.txt
#   make size       Blackfin code/data size of the port objects -> out/size.txt
#   make tables     regenerate src/grids_tables.cc from upstream resources.cc
#   make all        compare + sim + size

MI        ?= ../../mutableinstruments
BFIN_BIN  ?= $(HOME)/op1-toolchain/bfin/bin
PYTHON    ?= python3
BUILD     := build
OUT       := out
BARS      ?= 64

WARN := -Wall -Wno-unused-variable -Wno-unused-function

PORT_SRC := src/pattern_generator.cc src/drum_sequencer.cc src/grids_tables.cc
PORT_HDR := $(wildcard src/*.h)
UP_SRC   := $(MI)/grids/pattern_generator.cc $(MI)/grids/resources.cc shim/avrlib_random.cc
SHIM_HDR := $(wildcard shim/*/*.h)

HOST_CXX      := g++
HOST_CXXFLAGS := -O2 -g -std=c++11 -I. -Ishim -I$(MI) $(WARN)
BFIN_CXX      := $(BFIN_BIN)/bfin-elf-g++
BFIN_RUN      := $(BFIN_BIN)/bfin-elf-run
BFIN_SIZE     := $(BFIN_BIN)/bfin-elf-size
BFIN_CXXFLAGS := -O2 -std=c++11 -mcpu=bf524 -fno-exceptions -fno-rtti -DOP1_SIM -I. $(WARN)

.PHONY: all compare patterns sim size tables sim-grids clean

all: compare sim size

$(BUILD) $(OUT):
	mkdir -p $@

$(BUILD)/compare: tests/compare.cc $(PORT_SRC) $(PORT_HDR) $(UP_SRC) $(SHIM_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) tests/compare.cc $(PORT_SRC) $(UP_SRC) -o $@

compare: $(BUILD)/compare | $(OUT)
	$(BUILD)/compare | tee $(OUT)/compare.txt

patterns: $(BUILD)/compare | $(OUT)
	$(BUILD)/compare patterns > $(OUT)/patterns.txt; tail -n +8 $(OUT)/patterns.txt | head -40

$(BUILD)/bench_host: tests/bench.cc $(PORT_SRC) $(PORT_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) tests/bench.cc $(PORT_SRC) -o $@

$(BUILD)/bench_bfin.elf: tests/bench.cc $(PORT_SRC) $(PORT_HDR) | $(BUILD)
	$(BFIN_CXX) $(BFIN_CXXFLAGS) -msim tests/bench.cc $(PORT_SRC) -o $@

sim: $(BUILD)/bench_bfin.elf $(BUILD)/bench_host | $(OUT)
	$(BUILD)/bench_host $(BARS) > $(BUILD)/bench_host.txt
	$(BFIN_RUN) $(BUILD)/bench_bfin.elf $(BARS) | tee $(OUT)/sim.txt
	@h=$$(grep checksum $(BUILD)/bench_host.txt); b=$$(grep checksum $(OUT)/sim.txt); \
	 if [ "$$h" = "$$b" ]; then echo "sim: Blackfin $$b == host"; else echo "sim: MISMATCH host '$$h' bfin '$$b'"; exit 1; fi

# Freestanding objects as the OP-1 image would build them (no -msim, -Os
# and -O2), with a check for things the image can't have.
$(BUILD)/%.o: src/%.cc $(PORT_HDR) | $(BUILD)
	$(BFIN_CXX) $(BFIN_CXXFLAGS) -ffreestanding -c $< -o $@

size: $(BUILD)/pattern_generator.o $(BUILD)/drum_sequencer.o $(BUILD)/grids_tables.o | $(OUT)
	$(BFIN_SIZE) -A $^ | grep -E '^(\S+\.o|\.text|\.rodata|\.data|\.bss|\.init_array|\.ctors|Total)' | tee $(OUT)/size.txt
	@if $(BFIN_BIN)/bfin-elf-objdump -h $^ | grep -qE 'init_array|\.ctors'; then echo "size: static constructors present!"; exit 1; fi
	$(BFIN_BIN)/bfin-elf-ld -r -o $(BUILD)/grids_all.o $^
	@u=$$($(BFIN_BIN)/bfin-elf-nm -u $(BUILD)/grids_all.o | awk '{print $$2}' | tr '\n' ' '); \
	 echo "undefined symbols (helpers the image must provide): $${u:-none}" | tee -a $(OUT)/size.txt

# make sim-grids: the OP-1 controls (op1/grids_seq.*) on the host and in the
# Blackfin simulator.
GRIDS_SEQ_SRC := tests/sim_grids.cc op1/grids_seq.cc $(PORT_SRC)
$(BUILD)/sim_grids: $(GRIDS_SEQ_SRC) $(PORT_HDR) op1/grids_seq.h | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) $(GRIDS_SEQ_SRC) -o $@
$(BUILD)/sim_grids.elf: $(GRIDS_SEQ_SRC) $(PORT_HDR) op1/grids_seq.h | $(BUILD)
	$(BFIN_CXX) $(BFIN_CXXFLAGS) -msim $(GRIDS_SEQ_SRC) -o $@
sim-grids: $(BUILD)/sim_grids $(BUILD)/sim_grids.elf
	$(BUILD)/sim_grids
	$(BFIN_RUN) $(BUILD)/sim_grids.elf

tables:
	$(PYTHON) scripts/gen_tables.py $(MI) src/grids_tables.cc --stats

clean:
	rm -rf $(BUILD) $(OUT)
