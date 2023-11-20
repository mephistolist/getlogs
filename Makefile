CC=gcc
CFLAGS=-march=native -D_GNU_SOURCE -O2 -Wall -Wextra -fPIC --std=c17 -pipe -s

all:
	$(CC) -S -masm=intel getlogs.c $(CFLAGS)
	$(AS) getlogs.s -o getlogs.o
	$(CC) getlogs.o -o getlogs $(CFLAGS)
clean:
	rm -v getlogs.o getlogs.s
