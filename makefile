CFLAGS = -ansi -Wall -std=c99
LFLAGS = -lm
CC = gcc

all: overlay

overlay: main.o logic.o
    $(CC) $(LFLAGS) main.o logic.o -o overlay

main.o: main.c logic.h
    $(CC) $(CFLAGS) -c main.c

logic.o: logic.c logic.h
    $(CC) $(CFLAGS) -c logic.c

clean:
    rm main.o logic.o