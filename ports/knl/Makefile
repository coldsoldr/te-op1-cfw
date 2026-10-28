# Our effects as kennel plugins (docs/kennel-roadmap.md). Run inside WSL:
#
#   make            every plugin -> ../../build/knl/<id>.knl
#   make cs_plate   one of them
#   make svg        only the screens (from ../fx/op1/*.svg, category dots removed)
#   make host-test  the sequencer plugins' glue on the host (tests/*_host_test.cc)
#
# tools/knl_pack.py compiles, links and packs; tools/knl_svg.py strips the
# category dots from the firmware's screens.

REPO    := ../..
OUT     := $(REPO)/build/knl
PACK    := python3 $(REPO)/tools/knl_pack.py
SVG     := python3 $(REPO)/tools/knl_svg.py
FXSVG   := ../fx/op1

PLUGINS := bitcrusher drive tape_drive wavefolder freq_shift ensemble cs_plate cs_hall tape_echo \
           cs_clouds cs_clouds_delay rings marbles grids

# plugin screen = firmware screen (the reverbs and Clouds)
SCREEN_MAP := \
              cs_plate/plate.svg=$(FXSVG)/plate.svg cs_hall/hall.svg=$(FXSVG)/hall.svg \
              cs_clouds/clouds.svg=../clouds/op1/clouds.svg cs_clouds_delay/clouds.svg=../clouds/op1/clouds.svg \
              marbles/marbles.svg=../marbles/op1/marbles.svg grids/grids.svg=../grids/op1/grids.svg
SCREENS := $(foreach m,$(SCREEN_MAP),$(firstword $(subst =, ,$(m)))) rings/rings.svg bitcrusher/crush.svg drive/drive.svg tape_drive/tape_drive.svg wavefolder/fold.svg ensemble/ensemble.svg freq_shift/shift.svg tape_echo/tape_echo.svg

# Rings' kennel screen (id `rings`: nothing in hundekraft 3.0.3 uses it) is its own design (the firmware's rings.svg stays with the firmware).
rings/rings.svg: rings/gen_rings_knl_svg.py common/knl_glyphs.py
	python3 $< $@

# The bitcrusher's screen too (option C of the canvas "Bitcrusher screen").
bitcrusher/crush.svg: bitcrusher/gen_crush_knl_svg.py common/knl_glyphs.py
	python3 $< $@

# And drive's (design A "Schematic" of the canvas "Drive screen").
drive/drive.svg: drive/gen_drive_knl_svg.py common/knl_glyphs.py
	python3 $< $@

# And tape_drive's (design A "Reels, aligned" of the canvas "Tape screen"; was tapesat).
tape_drive/tape_drive.svg: tape_drive/gen_tape_drive_knl_svg.py common/knl_glyphs.py
	python3 $< $@

# And tape_echo's (design B "Schematic" of the canvas "Tape echo screen").
tape_echo/tape_echo.svg: tape_echo/gen_tape_echo_knl_svg.py common/knl_glyphs.py
	python3 $< $@

# And fold's (design A "Wave, aligned" of the canvas "Fold screen").
wavefolder/fold.svg: wavefolder/gen_fold_knl_svg.py common/knl_glyphs.py
	python3 $< $@

# And ensemble's (design C "Drift" of the canvas "Ensemble screen").
ensemble/ensemble.svg: ensemble/gen_ensemble_knl_svg.py common/knl_glyphs.py
	python3 $< $@

# And freq_shift's (design B "Dial" of the canvas "Freq shift screen").
freq_shift/shift.svg: freq_shift/gen_shift_knl_svg.py common/knl_glyphs.py
	python3 $< $@

.PHONY: all svg clean host-test $(PLUGINS)

all: $(PLUGINS)

svg: $(SCREENS)

# One rule per screen (a pattern rule can't hold two stems).
define SVG_RULE
$(firstword $(subst =, ,$(1))): $(lastword $(subst =, ,$(1))) $(REPO)/tools/knl_svg.py
	$$(SVG) $$< $$@
endef
$(foreach m,$(SCREEN_MAP),$(eval $(call SVG_RULE,$(m))))

define PLUGIN_RULE
$(1): $(filter $(1)/%,$(SCREENS))
	$(PACK) $(1) -o $(OUT)/$(1).knl
endef
$(foreach p,$(PLUGINS),$(eval $(call PLUGIN_RULE,$(p))))

# The sequencer features' glue on the host: the player's vtable functions,
# the type's callbacks and the settings text, against common/knl_seq_host.h's
# stand-ins for the core (tests/knl_seq_host.cc).
HOST_CXX      := g++
HOST_CXXFLAGS := -O2 -g -std=c++17 -DOP1_HOST -Wall -Wextra -Wno-unused-parameter
HOST_BUILD    := $(REPO)/build/knl/host
MARBLES_CORE  := ../marbles/op1/marbles_seq.cc ../marbles/src/t_generator_fx.cc ../marbles/src/x_y_generator_fx.cc \
                 ../marbles/src/output_channel_fx.cc ../marbles/src/quantizer_fx.cc ../marbles/src/tables_fx.cc
GRIDS_CORE    := ../grids/op1/grids_seq.cc ../grids/src/pattern_generator.cc ../grids/src/drum_sequencer.cc \
                 ../grids/src/grids_tables.cc

$(HOST_BUILD):
	mkdir -p $@

$(HOST_BUILD)/marbles_host_test: tests/marbles_host_test.cc tests/knl_seq_host.cc marbles/marbles_knl.cc $(MARBLES_CORE) common/knl_seq.h common/knl_seq_host.h | $(HOST_BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -I. -I.. -I../marbles tests/marbles_host_test.cc tests/knl_seq_host.cc marbles/marbles_knl.cc $(MARBLES_CORE) -o $@

$(HOST_BUILD)/grids_host_test: tests/grids_host_test.cc tests/knl_seq_host.cc grids/grids_knl.cc $(GRIDS_CORE) common/knl_seq.h common/knl_seq_host.h | $(HOST_BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) -I. -I.. -I../grids tests/grids_host_test.cc tests/knl_seq_host.cc grids/grids_knl.cc $(GRIDS_CORE) -o $@

host-test: $(HOST_BUILD)/marbles_host_test $(HOST_BUILD)/grids_host_test
	$(HOST_BUILD)/marbles_host_test
	$(HOST_BUILD)/grids_host_test

clean:
	rm -f $(OUT)/*.knl $(SCREENS)
	rm -rf $(HOST_BUILD)
