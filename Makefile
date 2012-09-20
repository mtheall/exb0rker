#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitPro")
endif

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

ifeq ($(strip $(FEOSSDK)),)
$(error "Please set FEOSSDK in your environment. export FEOSSDK=<path to>FeOS/sdk")
endif

ifeq ($(STAGE),2)

FEOSMK = $(FEOSSDK)/mk

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
#---------------------------------------------------------------------------------
TARGET        := $(shell basename $(CURDIR))
BUILD         := build
SOURCES       := source gfx
DATA          := data
INCLUDES      := include gfx

MANIFEST      := package.manifest
PACKAGENAME   := $(TARGET)

CONF_DEFINES       :=
CONF_USERLIBS      := coopgui feos3d
CONF_LIBS          := -lcoopgui -lfeos3d

include $(FEOSMK)/app.mk
include $(FEOSMK)/package.mk

install: all
	@mkdir -p $(FEOSDEST)/data/FeOS/bin
	@mkdir -p $(FEOSDEST)/data/FeOS/gui
	@cp $(TARGET).fx2 $(FEOSDEST)/data/FeOS/bin/$(TARGET).fx2
	@grit apptile.png -ftr -fh! -gb -gB16 -gT! -gzl -p! -o $(FEOSDEST)/data/FeOS/gui/$(TARGET).grf
	@fmantool application.manifest $(FEOSDEST)/data/FeOS/gui/$(TARGET).app

else
.PHONY: gfx gfx-clean

all: gfx
	@STAGE=2 $(MAKE) --no-print-directory

install: all
	@STAGE=2 $(MAKE) install --no-print-directory

clean: gfx-clean
	@STAGE=2 $(MAKE) clean --no-print-directory

gfx:
	@$(MAKE) -f gfx.mk

gfx-clean:
	@$(MAKE) -f gfx.mk clean
endif
