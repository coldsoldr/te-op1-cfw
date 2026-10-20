# ENSEMBLE (chorus -> string ensemble): included by ../Makefile.
#
#   make test-ensemble  the ensemble in the Blackfin simulator (assembly
#                       kernels = C, guard words, cycles per call, width,
#                       latency, 60 s stability) and on the host (C, plus
#                       the float reference's SNR); the C output's checksums
#                       must be equal on both

ENSEMBLE_SRC := src/ensemble/ensemble.cc
ENSEMBLE_ASM := src/ensemble/ensemble_bfin.S
ENSEMBLE_HDR := $(wildcard src/ensemble/*.h) ../rings/harness/cycles.h

.PHONY: test-ensemble

$(BUILD)/test_ensemble.elf: tests/test_ensemble.cc $(ENSEMBLE_SRC) $(ENSEMBLE_ASM) $(ENSEMBLE_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) tests/test_ensemble.cc $(ENSEMBLE_SRC) $(ENSEMBLE_ASM) -o $@

$(BUILD)/test_ensemble_host: tests/test_ensemble.cc $(ENSEMBLE_SRC) $(ENSEMBLE_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) tests/test_ensemble.cc $(ENSEMBLE_SRC) -o $@ -lm

test-ensemble: $(BUILD)/test_ensemble.elf $(BUILD)/test_ensemble_host
	@if $(BFIN_BIN)/bfin-elf-nm $(BUILD)/test_ensemble.elf | grep -q '_GLOBAL__sub_I'; then \
	  echo "test-ensemble: a static initialiser"; exit 1; fi
	@$(BFIN_RUN) $< > $(BUILD)/test_ensemble_bfin.txt; s=$$?; cat $(BUILD)/test_ensemble_bfin.txt; [ $$s = 0 ] || exit $$s
	@$(BUILD)/test_ensemble_host > $(BUILD)/test_ensemble_host.txt; s=$$?; sed 's/^/host  /' $(BUILD)/test_ensemble_host.txt; [ $$s = 0 ] || exit $$s
	@if [ "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_ensemble_bfin.txt)" = "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_ensemble_host.txt)" ]; \
	then echo "test-ensemble: Blackfin = host (checksums)"; else echo "test-ensemble: Blackfin DIFFERS from host"; \
	diff <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_ensemble_bfin.txt) <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_ensemble_host.txt); exit 1; fi
