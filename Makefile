#---------------------------------------------------------------------------------
.SUFFIXES:
#nicetest
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# GRAPHICS is a list of directories containing graphics files
# GFXBUILD is the directory where converted graphics files will be placed
#   If set to $(BUILD), it will statically link in the converted
#   files as if they were data files.
#
# NO_SMDH: if set to anything, no SMDH file is generated.
# ROMFS is the directory which contains the RomFS, relative to the Makefile (Optional)
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
# ICON is the filename of the icon (.png), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.png
#     - icon.png
#     - <libctru folder>/default_icon.png

#---------------------------------------------------------------------------------
# External tools
#---------------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
MAKEROM 	?= C:/devkitpro/tools/bin/makerom.exe
BANNERTOOL 	?= C:/devkitpro/tools/bin/bannertool.exe

else
MAKEROM 	?= makerom
BANNERTOOL 	?= bannertool

endif

# If on a tagged commit, use the tag instead of the commit
ifneq ($(shell echo $(shell git tag -l --points-at HEAD) | head -c 1),)
GIT_VER := $(shell git tag -l --points-at HEAD)
else
GIT_VER := $(shell git rev-parse --short HEAD)
endif

#---------------------------------------------------------------------------------
# Version number
#---------------------------------------------------------------------------------

VERSION_MAJOR := 1

VERSION_MINOR := 1

VERSION_MICRO := 0


#---------------------------------------------------------------------------------
TARGET		:=	CIA-Installer
BUILD		:=	build
SOURCES		:=	src src/quirc
DATA		:=	data
INCLUDES	:=	src src/quirc
#GRAPHICS	:=	gfx
#GFXBUILD	:=	$(BUILD)
#ROMFS		:=	romfs
#GFXBUILD	:=	$(ROMFS)/gfx
APP_TITLE := CIA Installer
APP_AUTHOR	:=	Zachary-Rude
APP_DESCRIPTION :=      Simple app for installing CIA files
ICON		:=	app/icon.png
BNR_IMAGE	:=  app/banner.png
BNR_AUDIO	:=	app/BannerAudio.wav
RSF_FILE	:=	app/build-cia.rsf

#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -Wno-psabi -O2 -mword-relocations \
			-DV_STRING=\"$(GIT_VER)\" \
			-fomit-frame-pointer -ffunction-sections \
			`curl-config --cflags` \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -D__3DS__ -D_GNU_SOURCE=1

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++20 

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -ljansson -lz -lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(CTRULIB) /usr/lib


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------


export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o)

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
			$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean

#---------------------------------------------------------------------------------
all: $(BUILD) $(GFXBUILD) $(DEPSDIR)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).cia $(TARGET).smdh app/*.bin 
	@rm -fr $(OUTDIR)

#---------------------------------------------------------------------------------
send:
	@3dslink -a $(IP) $(TARGET).3dsx
#---------------------------------------------------------------------------------
run:
	@flatpak run org.citra_emu.citra $(TARGET).3dsx
#---------------------------------------------------------------------------------
andsend:
	@make all
	@3dslink -a $(IP) $(TARGET).3dsx
#---------------------------------------------------------------------------------
cia: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile cia

#---------------------------------------------------------------------------------
3dsx: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile 3dsx

#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all: $(OUTPUT).cia $(OUTPUT).elf $(OUTPUT).3dsx

$(OUTPUT).elf	:	$(OFILES)

$(OUTPUT).cia	:	$(OUTPUT).elf $(OUTPUT).smdh
	$(BANNERTOOL) makebanner -i "../app/banner.png" -a "../app/BannerAudio.wav" -o "../app/banner.bin"

	$(BANNERTOOL) makesmdh -i "../app/icon.png" -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -o "../app/icon.bin"

	$(MAKEROM) -f cia -target t -exefslogo -o "../$(TARGET).cia" -elf "../$(TARGET).elf" -rsf "../app/build-cia.rsf" -banner "../app/banner.bin" -icon "../app/icon.bin" -logo "../app/logo.bcma.lz" -ver "$$(($(VERSION_MAJOR)*1024+$(VERSION_MINOR)*16+$(VERSION_MICRO)))"

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
