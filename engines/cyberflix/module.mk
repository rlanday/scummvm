MODULE := engines/cyberflix

MODULE_OBJS = \
	archive.o \
	cast.o \
	console.o \
	cyberflix.o \
	image.o \
	metaengine.o \
	puppet.o \
	saveload.o \
	script.o \
	set.o \
	shop.o \
	sound.o \
	stage.o \
	vm.o

# This module can be built as a plugin
ifeq ($(ENABLE_CYBERFLIX), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
