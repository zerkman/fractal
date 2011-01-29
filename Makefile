.SUFFIXES:
ifeq ($(strip $(PSL1GHT)),)
$(error "PSL1GHT must be set in the environment.")
endif

include $(PSL1GHT)/host/ppu.mk

TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCE		:=	source
INCLUDE		:=	include 
DATA		:=	data
LIBS		:=	-lgcm_sys -lreality -lsysutil -lio -lm

TITLE		:=	mandelbrot zoomer by zerkman / Sector One
APPID		:=	MAND00003
CONTENTID	:=	UP0001-$(APPID)_00-0000000000000000
PKGFILES	:=	release
ICON0		:=	ICON0.PNG

CFLAGS		+= -g -O2 -Wall --std=gnu99
CXXFLAGS	+= -g -O2 -Wall

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCE),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))
export BUILDDIR	:=	$(CURDIR)/$(BUILD)
export DEPSDIR	:=	$(BUILDDIR)

CFILES		:= $(foreach dir,$(SOURCE),$(notdir $(wildcard $(dir)/*.c)))
CXXFILES	:= $(foreach dir,$(SOURCE),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:= $(foreach dir,$(SOURCE),$(notdir $(wildcard $(dir)/*.S)))
BINFILES	:= $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin))) \
				spu.bin

export OFILES	:=	$(CFILES:.c=.o) \
					$(CXXFILES:.cpp=.o) \
					$(SFILES:.S=.o) \
					$(BINFILES:.bin=.bin.o)

export BINFILES	:=	$(BINFILES:.bin=.bin.h)

export INCLUDES	:=	$(foreach dir,$(INCLUDE),-I$(CURDIR)/$(dir)) \
					-I$(CURDIR)/$(BUILD)

.PHONY: $(BUILD) clean pkg run

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@make --no-print-directory -C spu
	@make --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@make --no-print-directory -C spu clean
	@echo "[RM]  $(notdir $(OUTPUT))"
	@rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).self $(OUTPUT).a $(OUTPUT)*.pkg

run: $(BUILD)
	@$(PS3LOADAPP) $(OUTPUT).self

pkg: $(BUILD) $(OUTPUT).pkg

else

DEPENDS	:= $(OFILES:.o=.d)

$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
$(OFILES): $(BINFILES)

-include $(DEPENDS)

endif
