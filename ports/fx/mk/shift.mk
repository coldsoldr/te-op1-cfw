# SHIFT (frequency shifter / ring modulator), included by ../Makefile.
#
#   make test-shift   kernels asm = C, asm instance = C instance, mix 0
#                     identity, sideband rejection, shift accuracy, spread,
#                     ring, feedback stability, cycles per call (Blackfin
#                     simulator), and the same measurements on the host (C);
#                     the C output's checksums must be equal on both.

SHIFT_SRC := src/shift/shift.cc
SHIFT_ASM := src/shift/shift_bfin.S
SHIFT_HDR := $(wildcard src/shift/*.h) ../rings/harness/cycles.h

.PHONY: test-shift

$(BUILD)/test_shift.elf: tests/test_shift.cc $(SHIFT_SRC) $(SHIFT_ASM) $(SHIFT_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) tests/test_shift.cc $(SHIFT_SRC) $(SHIFT_ASM) -o $@

$(BUILD)/test_shift_host: tests/test_shift.cc $(SHIFT_SRC) $(SHIFT_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) tests/test_shift.cc $(SHIFT_SRC) -o $@ -lm

test-shift: $(BUILD)/test_shift.elf $(BUILD)/test_shift_host
	@if $(BFIN_BIN)/bfin-elf-nm $(BUILD)/test_shift.elf | grep -q '_GLOBAL__sub_I'; then \
	  echo "test-shift: a static initialiser"; exit 1; fi
	@$(BFIN_RUN) $< > $(BUILD)/test_shift_bfin.txt; s=$$?; cat $(BUILD)/test_shift_bfin.txt; [ $$s = 0 ] || exit $$s
	@$(BUILD)/test_shift_host > $(BUILD)/test_shift_host.txt; s=$$?; sed 's/^/host  /' $(BUILD)/test_shift_host.txt; [ $$s = 0 ] || exit $$s
	@if [ "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_shift_bfin.txt)" = "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_shift_host.txt)" ]; \
	then echo "test-shift: Blackfin = host (checksums)"; else echo "test-shift: Blackfin DIFFERS from host"; \
	diff <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_shift_bfin.txt) <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_shift_host.txt); exit 1; fi
