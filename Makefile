export CC=gcc
export CFLAGS=-std=c99 -O0 -g -pthread -I/usr/X11/include -D_BSD_SOURCE -D_DEFAULT_SOURCE -pedantic -Werror -Wchar-subscripts -Wimplicit -Wsequence-point -Wwrite-strings -Wno-pointer-sign -Wno-parentheses -Wno-unused-result
export LDFLAGS=-pthread -L/usr/X11/lib -lm -lpng -lGL -lX11 -lxcb -lX11-xcb -lOSMesa

O=main.o world.o map.o resources.o battle.o movement.o combat.o pathfinding.o interface.o input.o input_map.o input_battle.o display.o image.o format.o json.o

all: $(O)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o map

check: clean
	$(MAKE) -C test check

clean:
	$(MAKE) -C test clean
	rm -f $(O)
	rm -f map
