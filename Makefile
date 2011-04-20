##
## Configuration
##

PREFIX          := ppu

TARGET		:= fractal.self

TITLE		:= mandelbrot zoomer
APPID		:= MAND00003
CONTENTID	:= UP0001-$(APPID)_00-0000000000000000
ICON0		:= ICON0.PNG
SFOXML		:= /usr/local/ps3dev/bin/sfo.xml

FLAGS		:= -std=gnu99
INCLUDES	:= -Iinclude -Idata
LIBS		:= -lgcm_sys -lrsx -lsysutil -lio -lrt -llv2 -lm

##
## Tools
##

AS		:= $(PSL1GHT)/$(PREFIX)/bin/$(PREFIX)-as
CC		:= $(PSL1GHT)/$(PREFIX)/bin/$(PREFIX)-gcc
CPP		:= $(PSL1GHT)/$(PREFIX)/bin/$(PREFIX)-g++
LD		:= $(PSL1GHT)/$(PREFIX)/bin/$(PREFIX)-g++

ASFLAGS		:=
CFLAGS		:= -Wall -O3 $(FLAGS) -I$(PSL1GHT)/$(PREFIX)/include $(INCLUDES)
CPPFLAGS	:= $(CFLAGS)
LDFLAGS		:= -L$(PSL1GHT)/$(PREFIX)/lib $(LIBS)

MAKE_SELF	:= $(PSL1GHT)/bin/fself.py
MAKE_SFO	:= $(PSL1GHT)/bin/sfo.py
MAKE_PKG	:= $(PSL1GHT)/bin/pkg.py

##
## Files
##

BINFILES	:= $(shell find data -name "*.bin" -print ) data/spu.bin
RAWFILES	:= $(shell find data -name "*.raw" -print )
CFILES		:= $(shell find source -name "*.c"   -print )
CPPFILES	:= $(shell find source -name "*.cpp" -print )
SFILES		:= $(shell find source -name "*.s"   -print )

OFILES		:= ${BINFILES:.bin=.o} ${CFILES:.c=.o} ${CPPFILES:.cpp=.o} ${SFILES:.s=.o}

##
## Rules
##

default: $(TARGET)

clean:
	@make --no-print-directory -C spu clean
	rm -rf $(OFILES) $(TARGET) data

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CPP) $(CPPFLAGS) -c $< -o $@

%.o: %.bin
	@$(PSL1GHT)/bin/bin2s -a 64 $< | $(AS) $(ASFLAGS) -o $@
	@echo "extern const u8"  `(echo $(<F) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"_end[];" >  `(echo $< | tr . _)`.h
	@echo "extern const u8"  `(echo $(<F) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"[];"     >> `(echo $< | tr . _)`.h
	@echo "extern const u32" `(echo $(<F) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`_size";"  >> `(echo $< | tr . _)`.h
	@rm $<

%.o: %.raw
	@$(PSL1GHT)/bin/bin2s -a 64 $< | $(AS) $(ASFLAGS) -o $@
	@echo "extern const u8"  `(echo $(<F) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"_end[];" >  `(echo $< | tr . _)`.h
	@echo "extern const u8"  `(echo $(<F) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"[];"     >> `(echo $< | tr . _)`.h
	@echo "extern const u32" `(echo $(<F) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`_size";"  >> `(echo $< | tr . _)`.h

%.o: %.s
	$(AS) $(ASFLAGS) -o $@

%.a: $(OFILES)
	$(AR) cru $@ $(OFILES)

%.elf: $(OFILES)
	$(LD) $(OFILES) $(LDFLAGS) -o $@

%.self: %.elf
	$(MAKE_SELF) $< $@ >> /dev/null

%.pkg: %.elf
	@rm -Rf pkg
	@mkdir -p pkg
	@mkdir -p pkg/USRDIR
	@cp $(ICON0) pkg
	$(MAKE_SELF) $< pkg/USRDIR/EBOOT.BIN $(CONTENTID) >> /dev/null
	$(MAKE_SFO) --title "$(TITLE)" --appid "$(APPID)" -f $(SFOXML) pkg/PARAM.SFO
	$(MAKE_PKG) --contentid $(CONTENTID) pkg/ $@ >> /dev/null
	@rm -Rf pkg

data/spu.bin:
	@$(MAKE) --no-print-directory -C spu
