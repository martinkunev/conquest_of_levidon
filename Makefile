export CC=gcc
export CFLAGS=-std=c99 -pthread -O2 -fstrict-aliasing -I/usr/X11/include -D_BSD_SOURCE -D_DEFAULT_SOURCE -pedantic -Werror -Wstrict-aliasing -Wchar-subscripts -Wimplicit -Wsequence
export LDFLAGS=-pthread -L/usr/X11/lib -lm -lpng -lGL -lX11 -lxcb -lX11-xcb -lOSMesa

O=main.o world.o map.o resources.o battle.o movement.o combat.o pathfinding.o interface.o display.o input.o input_map.o input_battle.o draw.o image.o format.o json.o

all: $(O)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o map

editor: image.o draw.o interface.o input.o json.o format.o editor.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o editor

check: clean
	$(MAKE) -C test check

clean:
	$(MAKE) -C test clean
	rm -f $(O)
	rm -f map
