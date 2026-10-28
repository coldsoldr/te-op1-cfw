# The effect engines an image or simulator build carries (ports/fx/op1/
# fx_host.cc): included by ports/rings/Makefile and ports/clouds/Makefile
# with FX_DIR set to ports/fx relative to them.
#
#   make op1-seq FX_ENGINES="plate drive"
#
# Each engine adds its glue (op1/<name>_fx.cc), its core's sources
# (src/<name>/*.cc, *.S) and -DOP1FX_<NAME>. Crush is always in, as is
# the module switch (op1/modules.cc: a mask word the builder patches).

FX_ENGINES ?= plate

FX_SRC := $(FX_DIR)/op1/fx_host.cc $(FX_DIR)/op1/modules.cc $(FX_DIR)/op1/crush_fx.cc \
          $(foreach e,$(FX_ENGINES),$(FX_DIR)/op1/$(e)_fx.cc \
            $(wildcard $(FX_DIR)/src/$(e)/*.cc $(FX_DIR)/src/$(e)/*.S))
FX_FLAGS := -I$(FX_DIR) -DOP1FX_DRIVE_ASM_ONLY $(foreach e,$(FX_ENGINES),-DOP1FX_$(shell echo $(e) | tr a-z A-Z))
FX_HDR := $(wildcard $(FX_DIR)/op1/*.h $(FX_DIR)/src/*/*.h)
