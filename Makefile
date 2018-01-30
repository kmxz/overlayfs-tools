CFLAGS = -Wall -g
LFLAGS = -lm
CC = gcc

all: overlay

objects = fsck.o common.o lib.o check.o mount.o path.o overlayfs.o

overlay: $(objects)
	$(CC) $(LFLAGS) $(objects) -o fsck.overlay

.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o fsck.overlay
	rm -rf bin

install: all
	mkdir bin
	cp fsck.overlay bin
