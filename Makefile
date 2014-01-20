CC=gcc
CFLAGS=-std=c99 -g -pthread -I/usr/X11/include -D_BSD_SOURCE -DTEST=0 -Werror -Wno-parentheses -Wchar-subscripts -Wimplicit -Wsequence-point
LDFLAGS=-pthread -L/usr/X11/lib -lm -lpng -lGL -lX11 -lxcb -lX11-xcb -lOSMesa

all: map

test: test.o
	$(CC) -L/usr/X11/lib -lGL -lOSMesa $^ -o test

map: map.o resources.o interface.o image.o battle.o format.o heap.o json.o dictionary.o vector.o
	$(CC) $(LDFLAGS) $^ -o map

battle: main.o interface.o image.o battle.o format.o heap.o json.o dictionary.o vector.o
	$(CC) $(LDFLAGS) $^ -o battle

polygons: polygons.o
	$(CC) $(LDFLAGS) $^ -o polygons

clean:
	rm battle.o heap.o format.o main.o resources.o map.o interface.o json.o dictionary.o vector.o
	rm battle
	rm x
