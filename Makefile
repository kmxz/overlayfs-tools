CFLAGS = -Wall -std=gnu11
LDLIBS = -lm
CC = gcc

all: overlay

objects_fsck = fsck.o common.o lib.o check.o mount.o path.o overlayfs.o
objects_tools = main.o logic.o sh.o
overlay: $(objects_tools) $(objects_fsck)
	$(CC) $(objects_tools) -o overlay $(LDLIBS)
	$(CC) $(objects_fsck) -o fsck.overlay

.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o overlay fsck.overlay

tests: overlay
	make -C test_cases clean
	make -C test_cases
