# TAPE (dirt category): included by ports/fx/Makefile.
#
#   make test-tape   in the Blackfin simulator (assembly kernels = C, the
#                    effect C = asm, mix 0 identity, silence, cycles per
#                    call) and on the host (the same C checks plus the
#                    analysis: harmonics and level against SATURATE, pitch
#                    deviation against WOBBLE, response against AGE, DC);
#                    the C output's checksums must be equal on both

TAPE_SRC := src/tape/tape.cc
TAPE_ASM := src/tape/tape_bfin.S
TAPE_HDR := $(wildcard src/tape/*.h) ../rings/harness/cycles.h

.PHONY: test-tape

$(BUILD)/test_tape.elf: tests/test_tape.cc $(TAPE_SRC) $(TAPE_ASM) $(TAPE_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) tests/test_tape.cc $(TAPE_SRC) $(TAPE_ASM) -o $@

$(BUILD)/test_tape_host: tests/test_tape.cc $(TAPE_SRC) $(TAPE_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) tests/test_tape.cc $(TAPE_SRC) -o $@ -lm

test-tape: $(BUILD)/test_tape.elf $(BUILD)/test_tape_host
	@if $(BFIN_BIN)/bfin-elf-nm $(BUILD)/test_tape.elf | grep -q '_GLOBAL__sub_I'; then \
	  echo "test-tape: a static initialiser"; exit 1; fi
	@$(BFIN_RUN) $< > $(BUILD)/test_tape_bfin.txt; s=$$?; cat $(BUILD)/test_tape_bfin.txt; [ $$s = 0 ] || exit $$s
	@$(BUILD)/test_tape_host > $(BUILD)/test_tape_host.txt; s=$$?; sed 's/^/host  /' $(BUILD)/test_tape_host.txt; [ $$s = 0 ] || exit $$s
	@if [ "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_tape_bfin.txt)" = "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_tape_host.txt)" ]; \
	then echo "test-tape: Blackfin = host (checksums)"; else echo "test-tape: Blackfin DIFFERS from host"; \
	diff <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_tape_bfin.txt) <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_tape_host.txt); exit 1; fi
