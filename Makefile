CC=gcc
CFLAGS=-std=c99 -g -pthread -I/usr/X11/include -D_BSD_SOURCE -DTEST=0 -Werror -Wno-parentheses -Wno-pointer-sign -Wno-empty-body -Wno-return-type -Wchar-subscripts -Wimplicit -Wsequence-point -Wno-switch
LDFLAGS=-pthread -L/usr/X11/lib -lm -lpng -lGL -lX11 -lxcb -lX11-xcb -lOSMesa

all: map

test: test.o
	$(CC) -L/usr/X11/lib -lGL -lOSMesa $^ -o test

map: main.o map.o resources.o interface.o input.o display.o image.o format.o json.o dictionary.o vector.o pathfinding.o battlefield.o
	$(CC) $(LDFLAGS) $^ -o map

#battle: main.o interface.o display.o image.o format.o json.o dictionary.o vector.o
#	$(CC) $(LDFLAGS) $^ -o battle

#polygons: polygons.o
#	$(CC) $(LDFLAGS) $^ -o polygons

clean:
	rm -f format.o main.o resources.o map.o interface.o image.o input.o display.o json.o dictionary.o vector.o pathfinding.o battlefield.o
	rm -f map
