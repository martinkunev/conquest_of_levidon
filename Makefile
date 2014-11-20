CC=gcc
CFLAGS=-std=c99 -g -pthread -I/usr/X11/include -D_BSD_SOURCE -DTEST=0 -Werror -Wno-parentheses -Wno-pointer-sign -Wno-empty-body -Wno-return-type -Wchar-subscripts -Wimplicit -Wsequence-point
LDFLAGS=-pthread -L/usr/X11/lib -lm -lpng -lGL -lX11 -lxcb -lX11-xcb -lOSMesa

all: map

test: test.o
	$(CC) -L/usr/X11/lib -lGL -lOSMesa $^ -o test

map: map.o resources.o interface.o display.o image.o battle.o format.o json.o dictionary.o vector.o
	$(CC) $(LDFLAGS) $^ -o map

battle: main.o interface.o display.o image.o battle.o format.o json.o dictionary.o vector.o
	$(CC) $(LDFLAGS) $^ -o battle

polygons: polygons.o
	$(CC) $(LDFLAGS) $^ -o polygons

clean:
	rm -f battle.o format.o main.o resources.o map.o interface.o display.o json.o dictionary.o vector.o
	rm -f battle
	rm -f x
