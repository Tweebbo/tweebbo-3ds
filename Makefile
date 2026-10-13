#---------------------------------------------------------------------------------
# Tweebbo 3DS. Build with ./build.sh (uses the devkitpro/devkitarm image).
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set. Use ./build.sh")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET      := tweebbo
BUILD       := obj
SOURCES     := source source/gen
INCLUDES    := source
ROMFS       := romfs
APP_TITLE   := Tweebbo
APP_DESCRIPTION := Tweebbo for Nintendo 3DS
APP_AUTHOR  := Tweebbo
ICON        := assets/icon.png

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections -std=gnu11 $(ARCH)
CFLAGS   += $(INCLUDE) -D__3DS__ -Wno-format-truncation -Wno-missing-field-initializers
CFLAGS   += -DAPP_VERSION='"$(shell cat $(TOPDIR)/VERSION)"'
ifneq ($(strip $(TWEEBBO_SERVER)),)
CFLAGS   += -DDEFAULT_SERVER='"$(TWEEBBO_SERVER)"'
endif
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS     := -lcitro2d -lcitro3d -ljansson -lmbedtls -lmbedx509 -lmbedcrypto -lctru -lm
LIBDIRS  := $(PORTLIBS) $(CTRULIB)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/out/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export LD := $(CC)
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export APP_ICON := $(TOPDIR)/$(ICON)
export _3DSXDEPS := $(OUTPUT).smdh
export _3DSXFLAGS += --smdh=$(OUTPUT).smdh --romfs=$(CURDIR)/$(ROMFS)

.PHONY: all clean

all: $(BUILD)
	@mkdir -p out
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@rm -fr $(BUILD) out

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS)
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
