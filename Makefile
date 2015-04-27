export CC=gcc
export CFLAGS=-std=c99 -g -pthread -I/usr/X11/include -D_BSD_SOURCE -D_DEFAULT_SOURCE -Werror -Wno-parentheses -Wno-empty-body -Wno-return-type -Wno-switch -Wchar-subscripts -Wimplicit -Wsequence-point
export LDFLAGS=-pthread -L/usr/X11/lib -lm -lpng -lGL -lX11 -lxcb -lX11-xcb -lOSMesa

all: main.o map.o resources.o interface.o input.o input_map.o input_battle.o display.o image.o format.o json.o dictionary.o vector.o pathfinding.o movement.o combat.o battle.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o map

check: clean
	$(MAKE) -C test check

clean:
	rm -f format.o main.o resources.o map.o interface.o image.o input_map.o input_battle.o display.o json.o dictionary.o vector.o pathfinding.o movement.o combat.o battle.o
	rm -f map
