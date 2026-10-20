# HALL (8-line FDN hall reverb): included by ../Makefile.
#
#   make test-hall     the hall in the Blackfin simulator (assembly kernels
#                      = C, guard words, cycles per call, 60 s stability)
#                      and on the host (C); the C output's checksums must be
#                      equal on both
#   make compare-hall  the fixed-point hall against its float reference
#                      (host): SNR, tail band energies, RT60, levels, the
#                      size-change click; WAVs in out/hall/

HALL_SRC := src/hall/hall.cc
HALL_ASM := src/hall/hall_bfin.S
HALL_HDR := $(wildcard src/hall/*.h) ../rings/harness/cycles.h

.PHONY: test-hall compare-hall

$(OUT)/hall:
	mkdir -p $@

$(BUILD)/test_hall.elf: tests/test_hall.cc $(HALL_SRC) $(HALL_ASM) $(HALL_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) tests/test_hall.cc $(HALL_SRC) $(HALL_ASM) -o $@

$(BUILD)/test_hall_host: tests/test_hall.cc $(HALL_SRC) $(HALL_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) tests/test_hall.cc $(HALL_SRC) -o $@ -lm

test-hall: $(BUILD)/test_hall.elf $(BUILD)/test_hall_host
	@if $(BFIN_BIN)/bfin-elf-nm $(BUILD)/test_hall.elf | grep -q '_GLOBAL__sub_I'; then \
	  echo "test-hall: a static initialiser"; exit 1; fi
	@$(BFIN_RUN) $< > $(BUILD)/test_hall_bfin.txt; s=$$?; cat $(BUILD)/test_hall_bfin.txt; [ $$s = 0 ] || exit $$s
	@$(BUILD)/test_hall_host > $(BUILD)/test_hall_host.txt; s=$$?; sed 's/^/host  /' $(BUILD)/test_hall_host.txt; [ $$s = 0 ] || exit $$s
	@if [ "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_hall_bfin.txt)" = "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_hall_host.txt)" ]; \
	then echo "test-hall: Blackfin = host (checksums)"; else echo "test-hall: Blackfin DIFFERS from host"; \
	diff <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_hall_bfin.txt) <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_hall_host.txt); exit 1; fi

$(BUILD)/hall_float_ref: tests/hall_float_ref.cc $(HALL_SRC) $(HALL_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) tests/hall_float_ref.cc $(HALL_SRC) -o $@ -lm

compare-hall: $(BUILD)/hall_float_ref | $(OUT)/hall
	$(BUILD)/hall_float_ref
