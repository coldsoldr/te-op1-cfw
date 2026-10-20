# FOLD (dirt category): included by ports/fx/Makefile.
#
#   make test-fold   in the Blackfin simulator (assembly kernels = C, the
#                    effect C = asm, mix 0 identity, DC, level, cycles per
#                    call) and on the host (the same C checks plus the
#                    aliasing measurement); the C output's checksums must be
#                    equal on both

FOLD_SRC := src/fold/fold.cc
FOLD_ASM := src/fold/fold_bfin.S
FOLD_HDR := $(wildcard src/fold/*.h) ../rings/harness/cycles.h

.PHONY: test-fold

$(BUILD)/test_fold.elf: tests/test_fold.cc $(FOLD_SRC) $(FOLD_ASM) $(FOLD_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) tests/test_fold.cc $(FOLD_SRC) $(FOLD_ASM) -o $@

$(BUILD)/test_fold_host: tests/test_fold.cc $(FOLD_SRC) $(FOLD_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) tests/test_fold.cc $(FOLD_SRC) -o $@ -lm

test-fold: $(BUILD)/test_fold.elf $(BUILD)/test_fold_host
	@if $(BFIN_BIN)/bfin-elf-nm $(BUILD)/test_fold.elf | grep -q '_GLOBAL__sub_I'; then \
	  echo "test-fold: a static initialiser"; exit 1; fi
	@$(BFIN_RUN) $< > $(BUILD)/test_fold_bfin.txt; s=$$?; cat $(BUILD)/test_fold_bfin.txt; [ $$s = 0 ] || exit $$s
	@$(BUILD)/test_fold_host > $(BUILD)/test_fold_host.txt; s=$$?; sed 's/^/host  /' $(BUILD)/test_fold_host.txt; [ $$s = 0 ] || exit $$s
	@if [ "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_fold_bfin.txt)" = "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_fold_host.txt)" ]; \
	then echo "test-fold: Blackfin = host (checksums)"; else echo "test-fold: Blackfin DIFFERS from host"; \
	diff <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_fold_bfin.txt) <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_fold_host.txt); exit 1; fi
