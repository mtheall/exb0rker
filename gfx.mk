ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitPro")
endif

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

GRIT := $(DEVKITARM)/bin/grit

GRITFILES := $(wildcard gfx/*.grit)

.PHONY: all clean $(GRITFILES)

all: $(GRITFILES)
	@mv *.s *.h gfx

$(GRITFILES):
	@echo $@
	@$(GRIT) -ff $@

clean:
	@rm -f gfx/*.s gfx/*.h
