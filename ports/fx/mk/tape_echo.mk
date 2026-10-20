# TAPE ECHO (src/tape_echo): included by ../Makefile.
#
#   make test-tape-echo  the tape echo in the Blackfin simulator (assembly
#                        kernels = C, guard words, impulse response, glide,
#                        cycles per call, 60 s stability) and on the host
#                        (C); the C output's checksums must be equal on both

TAPE_ECHO_SRC := src/tape_echo/tape_echo.cc
TAPE_ECHO_ASM := src/tape_echo/tape_echo_bfin.S
TAPE_ECHO_HDR := $(wildcard src/tape_echo/*.h) ../rings/harness/cycles.h

.PHONY: test-tape-echo

$(BUILD)/test_tape_echo.elf: tests/test_tape_echo.cc $(TAPE_ECHO_SRC) $(TAPE_ECHO_ASM) $(TAPE_ECHO_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) tests/test_tape_echo.cc $(TAPE_ECHO_SRC) $(TAPE_ECHO_ASM) -o $@

$(BUILD)/test_tape_echo_host: tests/test_tape_echo.cc $(TAPE_ECHO_SRC) $(TAPE_ECHO_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) tests/test_tape_echo.cc $(TAPE_ECHO_SRC) -o $@ -lm

test-tape-echo: $(BUILD)/test_tape_echo.elf $(BUILD)/test_tape_echo_host
	@if $(BFIN_BIN)/bfin-elf-nm $(BUILD)/test_tape_echo.elf | grep -q '_GLOBAL__sub_I'; then \
	  echo "test-tape-echo: a static initialiser"; exit 1; fi
	@$(BFIN_RUN) $< > $(BUILD)/test_tape_echo_bfin.txt; s=$$?; cat $(BUILD)/test_tape_echo_bfin.txt; [ $$s = 0 ] || exit $$s
	@$(BUILD)/test_tape_echo_host > $(BUILD)/test_tape_echo_host.txt; s=$$?; sed 's/^/host  /' $(BUILD)/test_tape_echo_host.txt; [ $$s = 0 ] || exit $$s
	@if [ "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_tape_echo_bfin.txt)" = "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_tape_echo_host.txt)" ]; \
	then echo "test-tape-echo: Blackfin = host (checksums)"; else echo "test-tape-echo: Blackfin DIFFERS from host"; \
	diff <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_tape_echo_bfin.txt) <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_tape_echo_host.txt); exit 1; fi
