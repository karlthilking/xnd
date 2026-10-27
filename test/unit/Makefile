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

TST_PATH_OBJECTS = tst-path.o path.o
TST_PATH_FLAGS = $(CFLAGS) -O0 $(ASAN) $(UBSAN)

TST_XND_ULOCK_OBJECTS = tst-xnd-ulock.o xnd-ulock.o
TST_XND_ULOCK_FLAGS = $(CFLAGS) -O2

TST_IO_OBJECTS = tst-io.o io.o
TST_IO_FLAGS = $(CFLAGS) -O0 $(ASAN) $(UBSAN)

TST_ATOMIC_FLAGS = $(CFLAGS) -O2 $(TSAN)

ALL = tst-path tst-xnd-ulock tst-io

DIRS = xnd xnd/util xnd/platform xnd/synch
VPATH = $(addprefix $(XND_ROOT)/,$(DIRS))

all: $(ALL)

%path.o: %path.c
	$(CC) $(TST_PATH_FLAGS) -c $< -o $@
tst-path: $(TST_PATH_OBJECTS)
	$(CC) $(TST_PATH_FLAGS) -o $@ $^
	dsymutil $@

%io.o: %io.c
	$(CC) $(TST_IO_FLAGS) -c $< -o $@
tst-io: $(TST_IO_OBJECTS)
	$(CC) $(TST_IO_FLAGS) -o $@ $^
	dsymutil $@

%ulock.o: %ulock.c
	$(CC) $(TST_XND_ULOCK_FLAGS) -c $< -o $@
tst-xnd-ulock: $(TST_XND_ULOCK_OBJECTS)
	$(CC) $(TST_XND_ULOCK_FLAGS) -o $@ $^
	dsymutil $@

tst-atomic: tst-atomic.c
	$(CC) $(TST_ATOMIC_FLAGS) -o $@ $<
	dsymutil $@

clean:
	rm -f $(ALL) *.o
	rm -rf *.dSYM

.PHONY: all clean
.INTERMEDIATE: \
	$(TST_PATH_OJBECTS) \
	$(TST_XND_ULOCK_OBJECTS) \
	$(TST_IO_OBJECTS)
