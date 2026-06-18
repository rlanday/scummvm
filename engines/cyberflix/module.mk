MODULE := engines/cyberflix

MODULE_OBJS = \
	audio/audio_runtime.o \
	audio/cbx_audio.o \
	cyberflix.o \
	metaengine.o \
	resources/archive.o \
	resources/cast.o \
	resources/image.o \
	resources/puppet.o \
	resources/set.o \
	resources/shop.o \
	resources/stage.o \
	runtime/actors.o \
	runtime/cursor.o \
	runtime/dispatch.o \
	runtime/loops.o \
	runtime/movie.o \
	runtime/palette.o \
	runtime/paths.o \
	runtime/presentation.o \
	runtime/props.o \
	runtime/puppet_runtime.o \
	runtime/set_runtime.o \
	runtime/set_state.o \
	runtime/stage_runtime.o \
	runtime/system.o \
	runtime/timing.o \
	saveload.o \
	ui/console.o \
	vm_host.o \
	vm_host_actor.o \
	vm_host_audio.o \
	vm_host_movie.o \
	vm_host_props.o \
	vm_host_puppet.o \
	vm_host_set_stage.o \
	vm/builtins.o \
	vm/script.o \
	vm/vm.o

# This module can be built as a plugin
ifeq ($(ENABLE_CYBERFLIX), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
