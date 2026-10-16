#---------------------------------------------------------------------------------
# Tweebbo 3DS. Build with ./build.sh (uses the devkitpro/devkitarm image).
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set. Use ./build.sh")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

BUILD_PROFILE ?= release
TARGET      := tweebbo
BUILD       := obj
OUTPUT_DIR  := out
ifeq ($(BUILD_PROFILE),debug)
TARGET      := tweebbo-debug
BUILD       := obj-debug
OUTPUT_DIR  := out/debug
endif
SOURCES     := source source/gen vendor/quickjs
INCLUDES    := source vendor/quickjs
ROMFS       := romfs
APP_TITLE   := Tweebbo
APP_DESCRIPTION := Tweebbo for Nintendo 3DS
ifeq ($(BUILD_PROFILE),debug)
APP_TITLE := Tweebbo Debug
APP_DESCRIPTION := Local test build - not for public release
endif
APP_AUTHOR  := Tweebbo
ICON        := assets/icon.png

ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS   := -g -Wall -O2 -mword-relocations -ffunction-sections -std=gnu11 $(ARCH)
CFLAGS   += $(INCLUDE) -D__3DS__ -Wno-format-truncation -Wno-missing-field-initializers
CFLAGS   += -DCONFIG_VERSION='"2026-06-04"' -D_GNU_SOURCE
ifeq ($(BUILD_PROFILE),debug)
CFLAGS   += -DTWEEBBO_DEBUG=1 -DAPP_VERSION='"$(shell cat $(TOPDIR)/VERSION)-debug"'
else
CFLAGS   += -DAPP_VERSION='"$(shell cat $(TOPDIR)/VERSION)"'
endif
ifneq ($(strip $(TWEEBBO_SERVER)),)
CFLAGS   += -DDEFAULT_SERVER='"$(TWEEBBO_SERVER)"'
endif
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS     := -lcitro2d -lcitro3d -ljansson -lmbedtls -lmbedx509 -lmbedcrypto -lctru -lm
LIBDIRS  := $(PORTLIBS) $(CTRULIB)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(OUTPUT_DIR)/$(TARGET)
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
	@printf '%s\n' '$(BUILD_PROFILE)|$(TWEEBBO_SERVER)|$(shell cat VERSION)' > $(BUILD)/config.next
	@cmp -s $(BUILD)/config.next $(BUILD)/config.stamp || { mv $(BUILD)/config.next $(BUILD)/config.stamp; rm -f $(BUILD)/*.o; }
	@mkdir -p $(OUTPUT_DIR)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@rm -fr $(BUILD) out

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS)
$(OUTPUT).elf: $(OFILES)

# QuickJS relies on wrapping signed arithmetic and loose pointer aliasing, like its
# upstream Makefile. Without these flags devkitARM GCC 16 drops the string iterator's
# index update, so every string iteration (spread, for-of, Array.from) never ends.
quickjs.o libregexp.o libunicode.o cutils.o dtoa.o: CFLAGS += -fwrapv -fno-strict-aliasing

-include $(DEPENDS)

endif
