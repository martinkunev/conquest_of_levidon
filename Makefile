CC=gcc
CFLAGS=-std=c99 -g -pthread -D_BSD_SOURCE -Werror -Wno-parentheses -Wchar-subscripts -Wimplicit -Wsequence-point
LDFLAGS=-pthread -lm -lGL -lglut -lX11

all: main.o battle.o format.o heap.o
	$(CC) $(LDFLAGS) $^ -o battle

opengl: format.o opengl.o
	$(CC) $(LDFLAGS) $^ -o opengl

clean:
	rm battle.o heap.o format.o main.o
	rm battle
