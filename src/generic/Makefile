export CC=gcc
export CFLAGS=-std=c99 -E -P

SUFFIX=

double: SUFFIX=_double
double: CFLAGS+= -Dheap_type='double' -Dheap_suffix=$(SUFFIX)
double:
	$(CC) $(CFLAGS) -DHEADER - < "heap.g" > "heap$(SUFFIX).h"
	$(CC) $(CFLAGS) -DSOURCE - < "heap.g" > "heap$(SUFFIX).c"
