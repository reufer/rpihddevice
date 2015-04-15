#
# Makefile for a Video Disk Recorder plugin
#
# $Id: Makefile 2.18 2013/01/12 13:45:01 kls Exp $

# The official name of this plugin.
# This name will be used in the '-P...' option of VDR to load the plugin.
# By default the main source file also carries this name.

PLUGIN = rpihddevice

### The version number of this plugin (taken from the main source file):

VERSION = $(shell grep 'static const char \*VERSION *=' $(PLUGIN).c | awk '{ print $$6 }' | sed -e 's/[";]//g')

### The directory environment:

# Use package data if installed...otherwise assume we're under the VDR source directory:
PKGCFG = $(if $(VDRDIR),$(shell pkg-config --variable=$(1) $(VDRDIR)/vdr.pc),$(shell PKG_CONFIG_PATH="$$PKG_CONFIG_PATH:../../.." pkg-config --variable=$(1) vdr))
LIBDIR = $(call PKGCFG,libdir)
LOCDIR = $(call PKGCFG,locdir)
PLGCFG = $(call PKGCFG,plgcfg)
#
TMPDIR ?= /tmp

### The compiler options:

export CFLAGS   = $(call PKGCFG,cflags)
export CXXFLAGS = $(call PKGCFG,cxxflags)

### The version number of VDR's plugin API:

APIVERSION = $(call PKGCFG,apiversion)

### Allow user defined options to overwrite defaults:

-include $(PLGCFG)

### The name of the distribution archive:

ARCHIVE = $(PLUGIN)-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### The name of the shared object file:

SOFILE = libvdr-$(PLUGIN).so

### Includes and Defines (add further entries here):

DEFINES += -DPLUGIN_NAME_I18N='"$(PLUGIN)"'
DEFINES += -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM
DEFINES += -Wno-psabi -Wno-write-strings -fpermissive

CXXFLAGS += -D__STDC_CONSTANT_MACROS

ILCDIR   =ilclient
VCINCDIR =$(SDKSTAGE)/opt/vc/include
VCLIBDIR =$(SDKSTAGE)/opt/vc/lib

INCLUDES += -I$(ILCDIR) -I$(VCINCDIR) -I$(VCINCDIR)/interface/vcos/pthreads 
INCLUDES += -I$(VCINCDIR)/interface/vmcs_host/linux
 
LDLIBS  += -lbcm_host -lvcos -lvchiq_arm -lopenmaxil -lGLESv2 -lEGL -lpthread -lrt
LDLIBS  += -Wl,--whole-archive $(ILCDIR)/libilclient.a -Wl,--no-whole-archive
LDFLAGS += -L$(VCLIBDIR)

DEBUG ?= 0
ifeq ($(DEBUG), 1)
    DEFINES += -DDEBUG
endif

DEBUG_BUFFERSTAT ?= 0
ifeq ($(DEBUG_BUFFERSTAT), 1)
    DEFINES += -DDEBUG_BUFFERSTAT
endif

DEBUG_BUFFERS ?= 0
ifeq ($(DEBUG_BUFFERS), 1)
    DEFINES += -DDEBUG_BUFFERS
endif

# ffmpeg/libav configuration
ifdef EXT_LIBAV
	LIBAV_PKGCFG = $(shell PKG_CONFIG_PATH=$(EXT_LIBAV)/lib/pkgconfig pkg-config $(1))
else
	LIBAV_PKGCFG = $(shell pkg-config $(1))
endif

LDLIBS   += $(call LIBAV_PKGCFG,--libs libavcodec) $(call LIBAV_PKGCFG,--libs libavformat)
INCLUDES += $(call LIBAV_PKGCFG,--cflags libavcodec) $(call LIBAV_PKGCFG,--cflags libavformat)

ifeq ($(call LIBAV_PKGCFG,--exists libswresample && echo 1), 1)
	DEFINES  += -DHAVE_LIBSWRESAMPLE
	LDLIBS   += $(call LIBAV_PKGCFG,--libs libswresample)
	INCLUDES += $(call LIBAV_PKGCFG,--cflags libswresample)
else
ifeq ($(call LIBAV_PKGCFG,--exists libavresample && echo 1), 1)
	DEFINES  += -DHAVE_LIBAVRESAMPLE
	LDLIBS   += $(call LIBAV_PKGCFG,--libs libavresample)
	INCLUDES += $(call LIBAV_PKGCFG,--cflags libavresample)
endif
endif

LDLIBS   += $(shell pkg-config --libs freetype2)
INCLUDES += $(shell pkg-config --cflags freetype2)

### The object files (add further files here):

ILCLIENT = $(ILCDIR)/libilclient.a
OBJS = $(PLUGIN).o setup.o omx.o audio.o omxdevice.o ovgosd.o display.o

### The main target:

all: $(SOFILE) i18n

### Implicit rules:

%.o: %.c
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) -o $@ $<

### Dependencies:

MAKEDEP = $(CXX) -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(CXXFLAGS) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.c) > $@

-include $(DEPFILE)

### Internationalization (I18N):

PODIR     = po
I18Npo    = $(wildcard $(PODIR)/*.po)
I18Nmo    = $(addsuffix .mo, $(foreach file, $(I18Npo), $(basename $(file))))
I18Nmsgs  = $(addprefix $(DESTDIR)$(LOCDIR)/, $(addsuffix /LC_MESSAGES/vdr-$(PLUGIN).mo, $(notdir $(foreach file, $(I18Npo), $(basename $(file))))))
I18Npot   = $(PODIR)/$(PLUGIN).pot

%.mo: %.po
	msgfmt -c -o $@ $<

$(I18Npot): $(wildcard *.c)
	xgettext -C -cTRANSLATORS --no-wrap --no-location -k -ktr -ktrNOOP --package-name=vdr-$(PLUGIN) --package-version=$(VERSION) --msgid-bugs-address='<see README>' -o $@ `ls $^`

%.po: $(I18Npot)
	msgmerge -U --no-wrap --no-location --backup=none -q -N $@ $<
	@touch $@

$(I18Nmsgs): $(DESTDIR)$(LOCDIR)/%/LC_MESSAGES/vdr-$(PLUGIN).mo: $(PODIR)/%.mo
	install -D -m644 $< $@

.PHONY: i18n
i18n: $(I18Nmo) $(I18Npot)

install-i18n: $(I18Nmsgs)

### Targets:

$(SOFILE): $(ILCLIENT) $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -shared $(OBJS) $(LDLIBS) -o $@

$(ILCLIENT):
	$(MAKE) --no-print-directory -C $(ILCDIR) all

install-lib: $(SOFILE)
	install -D $^ $(DESTDIR)$(LIBDIR)/$^.$(APIVERSION)

install: install-lib install-i18n

dist: $(I18Npo) clean
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@tar czf $(PACKAGE).tgz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(PACKAGE).tgz

clean:
	@-rm -f $(PODIR)/*.mo $(PODIR)/*.pot
	@-rm -f $(OBJS) $(DEPFILE) *.so *.tgz core* *~
	$(MAKE) --no-print-directory -C $(ILCDIR) clean

.PHONY:	cppcheck
cppcheck:
	@cppcheck --language=c++ --enable=all --suppress=unusedFunction -v -f .
