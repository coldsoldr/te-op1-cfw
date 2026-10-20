# DRIVE (dirt): an oversampled soft-clip overdrive (docs/drive-port.md).
# Included by ../Makefile, which defines BUILD, BFIN_CXX, SIM_CXXFLAGS,
# HOST_CXX, HOST_CXXFLAGS, BFIN_RUN and BFIN_BIN.
#
#   make test-drive   the drive in the Blackfin simulator (assembly passes =
#                     C, the effect C = asm, mix-0 identity, cycles per call)
#                     and on the host (C: the same checks plus harmonics,
#                     level, aliasing, DC); the C output's checksums must be
#                     equal on both, and the ELF has no static initialiser

DRIVE_SRC := src/drive/drive.cc
DRIVE_ASM := src/drive/drive_bfin.S
DRIVE_HDR := $(wildcard src/drive/*.h) ../rings/harness/cycles.h

.PHONY: test-drive

$(BUILD)/test_drive.elf: tests/test_drive.cc $(DRIVE_SRC) $(DRIVE_ASM) $(DRIVE_HDR) | $(BUILD)
	$(BFIN_CXX) $(SIM_CXXFLAGS) tests/test_drive.cc $(DRIVE_SRC) $(DRIVE_ASM) -o $@

$(BUILD)/test_drive_host: tests/test_drive.cc $(DRIVE_SRC) $(DRIVE_HDR) | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) tests/test_drive.cc $(DRIVE_SRC) -o $@ -lm

test-drive: $(BUILD)/test_drive.elf $(BUILD)/test_drive_host
	@if $(BFIN_BIN)/bfin-elf-nm $(BUILD)/test_drive.elf | grep -q '_GLOBAL__sub_I'; then \
	  echo "test-drive: a static initialiser"; exit 1; fi
	@$(BFIN_RUN) $< > $(BUILD)/test_drive_bfin.txt; s=$$?; cat $(BUILD)/test_drive_bfin.txt; [ $$s = 0 ] || exit $$s
	@$(BUILD)/test_drive_host > $(BUILD)/test_drive_host.txt; s=$$?; sed 's/^/host  /' $(BUILD)/test_drive_host.txt; [ $$s = 0 ] || exit $$s
	@if [ "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_drive_bfin.txt)" = "$$(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_drive_host.txt)" ]; \
	then echo "test-drive: Blackfin = host (checksums)"; else echo "test-drive: Blackfin DIFFERS from host"; \
	diff <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_drive_bfin.txt) <(grep -o 'checksum [0-9a-f]*' $(BUILD)/test_drive_host.txt); exit 1; fi
