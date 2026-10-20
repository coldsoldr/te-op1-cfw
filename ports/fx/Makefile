# New OP-1 effects (ports/fx): checks and comparisons. Run from this
# directory inside WSL (needs g++, and bfin-elf-g++ for the simulator
# targets; see tools/build_bfin_gcc.sh).
#
#   make test-plate     the plate in the Blackfin simulator (assembly
#                       kernels = C, guard words, cycles per call,
#                       60 s stability) and on the host (C); the C output's
#                       checksums must be equal on both
#   make compare-plate  the fixed-point plate against its float reference
#                       (host): SNR, tail band energies, the size-change
#                       click; WAVs in out/plate/
#
# ../rings/harness/cycles.h is shared by path, read-only.

SHELL     := /bin/bash
BFIN_BIN  ?= $(HOME)/op1-toolchain/bfin/bin
BUILD     := build
OUT       := out

WARN          := -Wall -Wno-unused-variable -Wno-unused-function
INC           := -I. -I..
HOST_CXX      := g++
HOST_CXXFLAGS := -O2 -g -std=c++17 $(INC) $(WARN)

BFIN_CXX      := $(BFIN_BIN)/bfin-elf-g++
BFIN_RUN      := $(BFIN_BIN)/bfin-elf-run
SIM_CXXFLAGS  := -O2 -std=c++17 -mcpu=bf524 -msim -fno-exceptions -fno-rtti -DOP1_SIM \
                 $(INC) $(WARN)

PLATE_SRC := src/plate/plate.cc
PLATE_ASM := src/plate/plate_bfin.S
PLATE_HDR := $(wildcard src/plate/*.h) ../rings/harness/cycles.h

.PHONY: test-plate compare-plate clean

$(BUILD) $(OUT)/plate:
	mkdir -p $@

$(BUILD)/test_plate.elf: tests/test_plate.cc $(PLATE_SRC) $(PLATE_ASM) $(PLATE_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) tests/test_plate.cc $(PLATE_SRC) $(PLATE_ASM) -o $@

$(BUILD)/test_plate_host: tests/test_plate.cc $(PLATE_SRC) $(PLATE_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) tests/test_plate.cc $(PLATE_SRC) -o $@ -lm

test-plate: $(BUILD)/test_plate.elf $(BUILD)/test_plate_host
	@if $(BFIN_BIN)/bfin-elf-nm $(BUILD)/test_plate.elf | grep -q '_GLOBAL__sub_I'; then \
	  echo "test-plate: a static initialiser"; exit 1; fi
	@$(BFIN_RUN) $< > $(BUILD)/test_plate_bfin.txt; s=$$?; cat $(BUILD)/test_plate_bfin.txt; [ $$s = 0 ] || exit $$s
	@$(BUILD)/test_plate_host > $(BUILD)/test_plate_host.txt; s=$$?; sed 's/^/host  /' $(BUILD)/test_plate_host.txt; [ $$s = 0 ] || exit $$s
	@if [ "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_plate_bfin.txt)" = "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_plate_host.txt)" ]; \
	then echo "test-plate: Blackfin = host (checksums)"; else echo "test-plate: Blackfin DIFFERS from host"; \
	diff <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_plate_bfin.txt) <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_plate_host.txt); exit 1; fi

$(BUILD)/plate_float_ref: tests/plate_float_ref.cc $(PLATE_SRC) $(PLATE_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) tests/plate_float_ref.cc $(PLATE_SRC) -o $@ -lm

compare-plate: $(BUILD)/plate_float_ref | $(OUT)/plate
	$(BUILD)/plate_float_ref

clean:
	rm -rf $(BUILD) $(OUT)

# One fragment per further effect (mk/<module>.mk): its own test targets,
# built with the variables above.
-include $(wildcard mk/*.mk)
