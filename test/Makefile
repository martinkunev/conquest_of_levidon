CFLAGS:=$(CFLAGS) -I../src/
LDFLAGS:=$(LDFLAGS) -lcmocka

#DEF=-D_GNU_SOURCE -DOS_BSD -D_BSD_SOURCE -D_FILE_OFFSET_BITS=64
#INC=-I$(CURDIR) -I$(CURDIR)/lib -I$(CURDIR)/../include
#LIB=-L/opt/lib
#WARNINGS=-Werror -Wchar-subscripts -Wimplicit -Wsequence-point -Wcast-align -Wstrict-aliasing -Wno-parentheses -Wno-comment -Wno-pointer-sign -Wno-return-type
##WARNINGS=-Werror -Wchar-subscripts -Wimplicit -Wsequence-point -Wcast-align -Wstrict-aliasing -Wno-parentheses -Wno-empty-body -Wno-comment

#export CC=gcc
#export CFLAGS=-std=c99 -fPIC -pthread -DRUN_MODE=1 -O2 -g -DDEBUG $(DEF) $(INC) $(WARNINGS) -Wno-unused-value
#export LDFLAGS=-pthread $(LIB) -lz -lcheck

format: format.o ../src/format.o
	$(CC) $^ $(LDFLAGS) -o $@

json: json.o ../src/json.o ../src/generic/array_json.o ../src/format.o
	$(CC) $^ $(LDFLAGS) -o $@

pathfinding: pathfinding.o ../src/battle.o ../src/movement.o
	$(CC) $^ $(LDFLAGS) -lm -o $@

map: map.o ../src/map.o ../src/world.o ../src/resources.o ../src/json.o ../src/generic/array_json.o ../src/format.o
	$(CC) $^ $(LDFLAGS) -Wl,--wrap=free -o $@

check: format json pathfinding map
	./format
	./json
	./pathfinding
	./map

clean:
	rm -f *.o
	rm -f format json pathfinding map
