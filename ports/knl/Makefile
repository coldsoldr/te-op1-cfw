# Our effects as kennel plugins (docs/kennel-roadmap.md). Run inside WSL:
#
#   make            every plugin -> ../../build/knl/<id>.knl
#   make cs_plate   one of them
#   make svg        only the screens (from ../fx/op1/*.svg, category dots removed)
#
# tools/knl_pack.py compiles, links and packs; tools/knl_svg.py strips the
# category dots from the firmware's screens.

REPO    := ../..
OUT     := $(REPO)/build/knl
PACK    := python3 $(REPO)/tools/knl_pack.py
SVG     := python3 $(REPO)/tools/knl_svg.py
FXSVG   := ../fx/op1

PLUGINS := bitcrusher drive tape_drive wavefolder freq_shift ensemble cs_plate cs_hall tape_echo \
           cs_clouds cs_clouds_delay rings

# plugin screen = firmware screen (the reverbs and Clouds)
SCREEN_MAP := \
              cs_plate/plate.svg=$(FXSVG)/plate.svg cs_hall/hall.svg=$(FXSVG)/hall.svg \
              cs_clouds/clouds.svg=../clouds/op1/clouds.svg cs_clouds_delay/clouds.svg=../clouds/op1/clouds.svg
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

.PHONY: all svg clean $(PLUGINS)

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

clean:
	rm -f $(OUT)/*.knl $(SCREENS)
