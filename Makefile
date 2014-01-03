CC=gcc
CFLAGS=-std=c99 -g -pthread -I/usr/X11/include -D_BSD_SOURCE -DTEST=0 -Werror -Wno-parentheses -Wchar-subscripts -Wimplicit -Wsequence-point
LDFLAGS=-pthread -L/usr/X11/lib -lm -lpng -lGL -lX11 -lxcb -lX11-xcb

all: main.o interface.o image.o battle.o format.o heap.o json.o dictionary.o vector.o
	$(CC) $(LDFLAGS) $^ -o battle

map: map.o resources.o interface.o image.o battle.o format.o heap.o json.o dictionary.o vector.o
	$(CC) $(LDFLAGS) $^ -o map

clean:
	rm battle.o heap.o format.o main.o
	rm battle
	rm x
