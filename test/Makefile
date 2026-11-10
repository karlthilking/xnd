XND-ROOT = ..
BUILD = bin

CC = clang
CXX = clang++
SWIFTC = swiftc

INCLUDE-DIRS = \
	$(XND-ROOT) \
	$(XND-ROOT)/xnd \
	$(XND-ROOT)/include \
	$(XND-ROOT)/support

INCLUDE-FLAGS = $(foreach dir,$(INCLUDE-DIRS),-iquote $(dir))

CFLAGS = -Wall -g -O2 $(INCLUDE-FLAGS)
CXXFLAGS = -std=c++20 -Wall -g -O2 $(INCLUDE-FLAGS)
SWIFTFLAGS = -g -Onone

OMP-PATH = /opt/homebrew/opt/libomp
OMP-LIB = $(OMP-PATH)/lib
OMP-INCLUDE = $(OMP-PATH)/include
OMP-CFLAGS = -Xpreprocessor -fopenmp -I$(OMP-INCLUDE)
OMP-LDFLAGS = -lomp -L$(OMP-LIB)

TESTS := \
	01_conjugate_gradient \
	02_heat_diffusion \
	03_nbody \
	04_jacobi \
	06-swift-await \
	07-swift-task-sleep \
	08-dispatch \
	09-workq-kevent

DIRS = xnd support include test
VPATH = $(addprefix $(XND-ROOT)/,$(DIRS))

ALL = $(addprefix $(BUILD)/,$(TESTS))
all: $(ALL)

$(BUILD):
	mkdir -p $@

objs-prefix = $(addprefix $(BUILD)/,$(1))
objs-suffix = $(addsuffix .$(1).o,$(basename $($(1)-SRCS)))
srcs-to-objs = $(call objs-prefix,$(call objs-suffix,$(1)))

workq-kevent-SRCS = 09-workq-kevent.c xworkqueue.c workqueue.c
workq-kevent-OBJS = $(call srcs-to-objs,workq-kevent)
$(BUILD)/%.workq-kevent.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@ 
$(BUILD)/09-workq-kevent: $(workq-kevent-OBJS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^
	dsymutil $@

heat-diffusion-SRCS = 02_heat_diffusion.c
heat-diffusion-OBJS = $(call srcs-to-objs,heat-diffusion)
$(BUILD)/%.heat-diffusion.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) $(OMP-CFLAGS) -c $< -o $@
$(BUILD)/02_heat_diffusion: $(heat-diffusion-OBJS) | $(BUILD)
	$(CC) $(CFLAGS) $(OMP-LDFLAGS) -o $@ $^

$(BUILD)/%: %.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/%: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BUILD)/%: %.swift | $(BUILD)
	$(SWIFTC) $(SWIFTFLAGS) -o $@ $<

OBJS = $(workq-kevent-OBJS) $(heat-diffusion-OBJS)

clean:
	rm -f $(ALL) $(OBJS)
	rm -rf $(ALL:%=%.dSYM)
	rm -rf *-checkpoints xnd.log __pycache__

.PHONY: clean all
.INTERMEDIATE: $(OBJS)
