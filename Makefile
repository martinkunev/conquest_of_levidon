CC=gcc
CFLAGS=-std=c99 -g -pthread -I/usr/X11/include -D_BSD_SOURCE -Werror -Wno-parentheses -Wchar-subscripts -Wimplicit -Wsequence-point
LDFLAGS=-pthread -L/usr/X11/lib -lm -lGL -lX11 -lxcb -lX11-xcb

all: main.o interface.o battle.o format.o heap.o
	$(CC) $(LDFLAGS) $^ -o battle

xlib: xlib.o
	$(CC) $(LDFLAGS) $^ -o x

test: working.o
	$(CC) $(LDFLAGS) $^ -o test

clean:
	rm battle.o heap.o format.o main.o
	rm battle
	rm x
