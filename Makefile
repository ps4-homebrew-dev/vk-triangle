#   Copyright (C) 2026 John Törnblom
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING. If not see
# <http://www.gnu.org/licenses/>.

PS4_HOST ?= ps4

TITLE      := Vulkan Triangle
VERSION    := 1.00
TITLE_ID   := BREW95438
CONTENT_ID := IV0000-$(TITLE_ID)_00-VULKANTRIANGLE00

ifdef PS4_HOMEBREW_SDK
    include $(PS4_HOMEBREW_SDK)/toolchain/orbis.mk
else
    $(error PS4_HOMEBREW_SDK is undefined)
endif

CFLAGS := -Ishaders
CFLAGS += `$(PKG_CONFIG) vulkan --cflags --libs`

PKGFILES := eboot.bin sce_module/libvulkan.so.1 sce_sys/param.sfo sce_sys/icon0.png

all: $(CONTENT_ID).pkg

main.c: shaders/triangle_vert.h shaders/triangle_frag.h

shaders/triangle_vert.h: shaders/triangle.vert
	glslangValidator -V --vn triangle_vert -o $@ $^

shaders/triangle_frag.h: shaders/triangle.frag
	glslangValidator -V --vn triangle_frag -o $@ $^

eboot.elf: main.c
	$(CC) $(CFLAGS) -o $@ $(LDADD) $^

eboot.bin: eboot.elf
	$(ELF2SELF)  $^ $@

sce_sys/icon0.png:
	@mkdir -p $(@D)
	convert -size 512x512 -gravity center label:'$(TITLE)' $@

sce_sys/param.sfo:
	@mkdir -p $(@D)
	$(MKSFO) -i APP_TYPE=1 \
		 -s128 TITLE='$(TITLE)' \
		 -s12 TITLE_ID=$(TITLE_ID) \
		 -s48 CONTENT_ID=$(CONTENT_ID) \
		 -s8 APP_VER=$(VERSION) \
		 -s8 VERSION=$(VERSION) \
		 -i ATTRIBUTE=0 \
		 -i DOWNLOAD_DATA_SIZE=0 \
		 -i SYSTEM_VER=0 \
		 -s4 CATEGORY=gd \
		 $@

sce_module/libvulkan.so.1: $(SYSROOT)/$(PREFIX)/lib/libvulkan.so.1
	@mkdir -p $(@D)
	$(ELF2SELF)  $^ $@

$(CONTENT_ID).gp4: $(PKGFILES)
	$(MKGP4) -c $(CONTENT_ID) -o $@ $^

$(CONTENT_ID).pkg: $(CONTENT_ID).gp4 $(PKGFILES)
	$(MKPKG) -o $@ $<

clean:
	rm -f eboot.bin *.elf *.gp4 *.pkg shaders/*.h sce_module/*.* sce_sys/*.*
