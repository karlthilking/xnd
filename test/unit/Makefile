CC = clang
CFLAGS = -g -O0 -Wall -Werror -D_XOPEN_SOURCE

XND_ROOT = ../..
override CFLAGS += \
	-iquote $(XND_ROOT) \
	-iquote $(XND_ROOT)/xnd \
	-iquote $(XND_ROOT)/include

TST_PATH_OBJECTS = \
	tst-path.o \
	path.o

TST_XND_ULOCK_OBJECTS = \
	tst-xnd-ulock.o \
	xnd-ulock.o

ALL = tst-path tst-xnd-ulock

DIRS = xnd xnd/util xnd/platform xnd/synch
VPATH = $(addprefix $(XND_ROOT)/,$(DIRS))

all: $(ALL)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

tst-path: $(TST_PATH_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

tst-xnd-ulock: $(TST_XND_ULOCK_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(ALL) *.o
	rm -rf *.dSYM

.PHONY: all clean
.INTERMEDIATE: $(TST_PATH_OJBECTS) $(TST_XND_ULOCK_OBJECTS)
