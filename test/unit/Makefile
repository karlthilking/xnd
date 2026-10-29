CC = clang
CFLAGS = -g -Wall -Werror
ASAN = -fsanitize=address
TSAN = -fsanitize=thread
UBSAN = -fsanitize=undefined

XND_ROOT = ../..
override CFLAGS += \
	-iquote $(XND_ROOT) \
	-iquote $(XND_ROOT)/xnd \
	-iquote $(XND_ROOT)/include

ALL = tst-path tst-xnd-ulock tst-atomic tst-io tst-thread-port

DIRS = xnd xnd/util xnd/platform xnd/synch
VPATH = $(addprefix $(XND_ROOT)/,$(DIRS))

all: $(ALL)

tst-path-SRCS = tst-path.c path.c
tst-path-OBJS = $(tst-path-SRCS:.c=.path.o)
tst-path-FLAGS = $(CFLAGS) -O0 $(ASAN) $(UBSAN)
%.path.o: %.c
	$(CC) $(tst-path-FLAGS) -c $< -o $@
tst-path: $(tst-path-OBJS)
	$(CC) $(tst-path-FLAGS) -o $@ $^
	dsymutil $@

tst-io-SRCS = tst-io.o io.o
tst-io-OBJS = $(tst-io-SRCS:.c=.io.o)
tst-io-FLAGS = $(CFLAGS) -O0 $(ASAN) $(UBSAN)
%.io.o: %.c
	$(CC) $(tst-io-FLAGS) -c $< -o $@
tst-io: $(tst-io-OBJS)
	$(CC) $(tst-io-FLAGS) -o $@ $^
	dsymutil $@

tst-xnd-ulock-SRCS = xnd-ulock.c tst-xnd-ulock.c
tst-xnd-ulock-OBJS = $(tst-xnd-ulock-SRCS:.c=.xnd-ulock.o)
tst-xnd-ulock-FLAGS = $(CFLAGS) -O2
%.xnd-ulock.o: %.c
	$(CC) $(tst-xnd-ulock-FLAGS) -c $< -o $@
tst-xnd-ulock: $(tst-xnd-ulock-OBJS)
	$(CC) $(tst-xnd-ulock-FLAGS) -o $@ $^
	dsymutil $@

tst-atomic-FLAGS = $(CFLAGS) -O2 $(TSAN)
tst-atomic: tst-atomic.c
	$(CC) $(tst-atomic-FLAGS) -o $@ $<

tst-thread-port-SRCS = tst-thread-port.c thread-port.c
tst-thread-port-OBJS = $(tst-thread-port-SRCS:.c=.thread-port.o)
tst-thread-port-FLAGS = $(CFLAGS) -O2 $(ASAN) $(UBSAN)
%.thread-port.o: %.c
	$(CC) $(tst-thread-port-FLAGS) -c $< -o $@
tst-thread-port: $(tst-thread-port-OBJS)
	$(CC) $(tst-thread-port-FLAGS) -o $@ $^
	dsymutil $@

clean:
	rm -f $(ALL) *.o
	rm -rf *.dSYM

.DEFAULT: all
.PHONY: all clean
.INTERMEDIATE: \
	$(tst-path-OBJS) $(tst-io-OBJS) $(tst-xnd-ulock-OBJS) \
	$(tst-thread-port-OBJS)
