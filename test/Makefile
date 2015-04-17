CFLAGS:=$(CFLAGS) -I../ -DTEST
LDFLAGS:=$(LDFLAGS) -lcheck

#DEF=-D_GNU_SOURCE -DOS_BSD -D_BSD_SOURCE -D_FILE_OFFSET_BITS=64
#INC=-I$(CURDIR) -I$(CURDIR)/lib -I$(CURDIR)/../include
#LIB=-L/opt/lib
#WARNINGS=-Werror -Wchar-subscripts -Wimplicit -Wsequence-point -Wcast-align -Wstrict-aliasing -Wno-parentheses -Wno-comment -Wno-pointer-sign -Wno-return-type
##WARNINGS=-Werror -Wchar-subscripts -Wimplicit -Wsequence-point -Wcast-align -Wstrict-aliasing -Wno-parentheses -Wno-empty-body -Wno-comment

#export CC=gcc
#export CFLAGS=-std=c99 -fPIC -pthread -DRUN_MODE=1 -O2 -g -DDEBUG $(DEF) $(INC) $(WARNINGS) -Wno-unused-value
#export LDFLAGS=-pthread $(LIB) -lz -lcheck

check: format 
	./format

format: ../format.o
	$(CC) $^ $(LDFLAGS) -o $@

clean:
	rm -f *.o
	rm -f format
