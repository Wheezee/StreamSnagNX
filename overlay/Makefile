#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

APP_TITLE	:= StreamSnag
APP_AUTHOR	:= Wheezee
APP_VERSION	:= 0.1.0

TARGET		:= StreamSnag
BUILD		:= build
SOURCES		:= source
INCLUDES	:= source libtesla/include ../sys-snag/include

NO_ICON		:= 1

ARCH		:= -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS		:= -g -Wall -O2 -ffunction-sections -fdata-sections \
			   $(ARCH) $(DEFINES)

CFLAGS		+= $(INCLUDE) -D__SWITCH__

CXXFLAGS	:= $(CFLAGS) -fno-exceptions -std=c++20

ASFLAGS		:= -g $(ARCH)
LDFLAGS		= -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map) -Wl,--gc-sections

LIBS		:= -lnx
LIBDIRS		:= $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:= $(CURDIR)/$(TARGET)
export TOPDIR	:= $(CURDIR)

export VPATH	:= $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR	:= $(CURDIR)/$(BUILD)

CPPFILES	:= $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
export LD	:= $(CXX)
export OFILES := $(CPPFILES:.cpp=.o)

export INCLUDE	:= $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
				   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
				   -I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# Pack the NACP (name/version metadata) into the .ovl, like sys-tune does.
# Without --nacp, loaders (Tesla/Ultrahand) can't read the overlay name
# and silently skip the file. Must live in this first-phase block: CURDIR
# here is the project dir (in the build/ phase it would be wrong).
export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).ovl $(TARGET).elf $(TARGET).nacp

else

DEPENDS	:= $(OFILES:.o=.d)

all : $(OUTPUT).ovl

$(OUTPUT).ovl : $(OUTPUT).elf $(OUTPUT).nacp
	@elf2nro $< $@ $(NROFLAGS)
	@echo "built ... $(notdir $(OUTPUT).ovl)"

$(OUTPUT).elf : $(OFILES)

-include $(DEPENDS)

endif
