ARCH            ?= arm64
BUILD           ?= .
TIMING          ?= 0
DEVELOPMENT     ?= 1
DEBUG           ?= 0

CC      := clang
CXX     := clang++

ifeq ($(DEBUG),1)
OPTFLAGS := -O0
else
OPTFLAGS := -O2
endif

CFLAGS := \
	-std=c17 -Wall -Wno-deprecated-declarations \
	-Wimplicit-fallthrough -D_XOPEN_SOURCE \
	-DDEVELOPMENT=$(DEVELOPMENT) -DTIMING=$(TIMING) -DDEBUG=$(DEBUG) \
	-g $(OPTFLAGS) -arch $(ARCH) -iquote . -iquote ./include

CXXFLAGS := \
	-std=c++20 -Wall -Wno-deprecated-declarations \
	-Wimplicit-fallthrough -D_XOPEN_SOURCE \
	-DDEVELOPMENT=$(DEVELOPMENT) -DTIMING=$(TIMING) -DDEBUG=$(DEBUG) \
	-g $(OPTFLAGS) -arch $(ARCH) -iquote . -iquote ./include

LIBXND_OBJS := \
        $(BUILD)/xnd_lib.o \
        $(BUILD)/pac.o \
        $(BUILD)/vm_common.o \
        $(BUILD)/vm_checkpoint.o \
        $(BUILD)/writeckpt.o \
        $(BUILD)/shared_cache.o \
        $(BUILD)/thread_info.o \
	$(BUILD)/tls.o \
        $(BUILD)/ckptfile.o \
        $(BUILD)/log.o \
        $(BUILD)/path.o \
        $(BUILD)/debug.o \
        $(BUILD)/signal.o \
        $(BUILD)/_setcontext.o \
        $(BUILD)/setcontext.o \
        $(BUILD)/pid_table.o \
        $(BUILD)/pid_table_common.o \
        $(BUILD)/io.o \
        $(BUILD)/exe.o \
        $(BUILD)/xnd_coord_api.o \
        $(BUILD)/xnd_coord_client.o \
	$(BUILD)/xnd_coord_common.o \
        $(BUILD)/time_wrappers.o \
        $(BUILD)/stdlib_wrappers.o \
        $(BUILD)/signal_wrappers.o \
        $(BUILD)/pthread_wrappers.o \
        $(BUILD)/file_wrappers.o \
        $(BUILD)/fd_table.o \
        $(BUILD)/pid_wrappers.o \
        $(BUILD)/fd.o \
        $(BUILD)/env.o \
        $(BUILD)/compress.o \
	$(BUILD)/wq_wrappers.o \
	$(BUILD)/workq.o

XND_RESTART_INTERNAL_SRCS := \
        xnd/xnd_restart_internal.c \
        xnd/pac.c \
        xnd/vm_common.c \
        xnd/vm_restore.c \
        xnd/readckpt.c \
        xnd/shared_cache.c \
	xnd/util/io.c \
        xnd/util/log.c \
        xnd/util/path.c \
        xnd/platform/ucontext/setcontext.c \
        xnd/platform/ucontext/_setcontext.s

XND_LAUNCH_OBJS := \
        $(BUILD)/xnd_launch.o \
        $(BUILD)/ckptfile.o \
        $(BUILD)/exe.o \
        $(BUILD)/macho.o \
        $(BUILD)/path.o \
        $(BUILD)/shared_cache.o \
        $(BUILD)/io.o \
        $(BUILD)/log.o \
        $(BUILD)/env.o \
        $(BUILD)/xnd_coord_api.o \
	$(BUILD)/xnd_coord_common.o

XND_COMMAND_OBJS := \
	$(BUILD)/xnd_command.o \
	$(BUILD)/log.o \
	$(BUILD)/path.o \
	$(BUILD)/io.o \
	$(BUILD)/exe.o \
	$(BUILD)/xnd_coord_api.o \
	$(BUILD)/xnd_coord_common.o

XND_COORD_OBJS := \
        $(BUILD)/shared_cache.o \
        $(BUILD)/ckptfile.o \
        $(BUILD)/io.o \
        $(BUILD)/log.o \
        $(BUILD)/fd.o \
        $(BUILD)/env.o \
        $(BUILD)/path.o \
        $(BUILD)/pid_table_common.o \
        $(BUILD)/proc_list.o \
        $(BUILD)/xnd_coord.o \
	$(BUILD)/xnd_coord_common.o

XND_RESTART_OBJS := \
        $(BUILD)/xnd_restart.o \
        $(BUILD)/exe.o \
        $(BUILD)/path.o \
        $(BUILD)/io.o \
        $(BUILD)/env.o \
        $(BUILD)/xnd_coord_api.o \
	$(BUILD)/xnd_coord_common.o \
        $(BUILD)/shared_cache.o \
        $(BUILD)/log.o \
        $(BUILD)/ckptfile.o \
        $(BUILD)/compress.o

XND_PRINT_OBJS := \
        $(BUILD)/xnd_print.o \
        $(BUILD)/compress.o \
        $(BUILD)/log.o \
        $(BUILD)/path.o \
        $(BUILD)/io.o

XND_MACHO_PARSE_OBJS := \
	$(BUILD)/xnd_macho_parse.o \
	$(BUILD)/io.o \
	$(BUILD)/log.o \
	$(BUILD)/path.o \
	$(BUILD)/macho.o

ALL := \
        $(BUILD)/xnd_launch \
        $(BUILD)/xnd_print \
        $(BUILD)/xnd_restart \
        $(BUILD)/xnd_restart_internal \
        $(BUILD)/xnd_command \
        $(BUILD)/xnd_coordinator \
        $(BUILD)/libxnd.dylib \
	$(BUILD)/xnd_macho_parse

all: $(ALL)

debug:
	$(MAKE) clean
	$(MAKE) DEBUG=1 all

release:
	$(MAKE) clean
	$(MAKE) DEBUG=0 all

$(BUILD):
	mkdir -p $@

vpath %.c xnd xnd/util xnd/platform xnd/platform/ucontext xnd/coordinator xnd/pid xnd/wrappers
vpath %.cpp xnd xnd/pid
vpath %.s xnd/platform/ucontext

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

XRCC_FLAGS := $(CFLAGS) -fsanitize=address,undefined
$(BUILD)/xrcc: xrcc.c | $(BUILD)/segpatch
	$(CC) $(XRCC_FLAGS) -o $@ $<

XRCC := $(BUILD)/xrcc
$(BUILD)/xnd_restart_internal: $(XND_RESTART_INTERNAL_SRCS) | $(XRCC)
	BUILD=$(BUILD) \
	$(XRCC) $(CFLAGS) -fno-stack-protector \
	-o $@ $^ > $(BUILD)/xnd-restart.defs

$(BUILD)/xnd-restart.defs: $(BUILD)/xnd_restart_internal

$(BUILD)/vm_checkpoint.o: vm_checkpoint.c | $(BUILD)/xnd-restart.defs
	$(CC) $(CFLAGS) \
	$(shell cat $(BUILD)/xnd-restart.defs) \
	-c $< -o $@

$(BUILD)/libxnd.dylib: $(LIBXND_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -dynamiclib -fPIC -lz -o $@ $^
	dsymutil $@

$(BUILD)/xnd_restart: $(XND_RESTART_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -lz -o $@ $^
	dsymutil $@

$(BUILD)/xnd_launch: $(XND_LAUNCH_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^
	dsymutil $@

$(BUILD)/xnd_print: $(XND_PRINT_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) -lz -o $@ $^
	dsymutil $@

$(BUILD)/xnd_command: $(XND_COMMAND_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^
	dsymutil $@

$(BUILD)/xnd_coordinator: $(XND_COORD_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $^
	dsymutil $@

$(BUILD)/xnd_macho_parse: $(XND_MACHO_PARSE_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^
	dsymutil $@

$(BUILD)/segpatch: xnd/segpatch.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

test:	
	$(MAKE) -C test all

clean:
	rm -rf *.dSYM *-checkpoints
	rm -f $(ALL) *.dylib *.o *.ckpt *.dat *.xnd
	rm -f xnd.log xnd-debug.sh xnd_coordinator_v0
	rm -f $(BUILD)/segpatch $(BUILD)/xrcc $(BUILD)/*.defs

.PHONY: all clean test
.INTERMEDIATE: \
	$(LIBXND_OBJS) \
	$(XND_RESTART_OBJS) \
	$(XND_COORD_OBJS) \
	$(XND_PRINT_OBJS) \
	$(XND_MACHO_PARSE_OBJS) \
	$(XND_LAUNCH_OBJS) \
	$(XND_COMMAND_OBJS) \
	$(BUILD)/segpatch \
	$(BUILD)/xrcc \
	$(BUILD)/xnd-restart.defs
