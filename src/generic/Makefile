export CC=gcc
export CFLAGS=-std=c99 -pedantic -E -P

SUFFIX=

double: NAME=heap_double
double: CFLAGS+= -Dheap_type='double' -Dheap_name=$(NAME)
double:
	$(CC) $(CFLAGS) -DHEADER - < "heap.g" > "$(NAME).h"
	$(CC) $(CFLAGS) -DSOURCE -DHEADER_NAME=$(NAME).h - < "heap.g" > "$(NAME).c"
