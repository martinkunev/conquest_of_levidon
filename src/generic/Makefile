export CC=gcc
export CFLAGS=-std=c99 -pedantic -E -P

SUFFIX=

array_json: NAME=array_json
array_json: CFLAGS+= -Darray_type='union json *' -Darray_name=$(NAME)
array_json:
	$(CC) $(CFLAGS) -DHEADER - < "array.g" > "$(NAME).h"
	$(CC) $(CFLAGS) -DSOURCE -DHEADER_NAME=$(NAME).h - < "array.g" > "$(NAME).c"

array_moves: NAME=array_moves
array_moves: CFLAGS+= -Darray_type='struct move' -Darray_name=$(NAME)
array_moves:
	$(CC) $(CFLAGS) -DHEADER - < "array.g" > "$(NAME).h"
	$(CC) $(CFLAGS) -DSOURCE -DHEADER_NAME=$(NAME).h - < "array.g" > "$(NAME).c"

####

double: NAME=heap_double
double: CFLAGS+= -Dheap_type='double' -Dheap_name=$(NAME)
double:
	$(CC) $(CFLAGS) -DHEADER - < "heap.g" > "$(NAME).h"
	$(CC) $(CFLAGS) -DSOURCE -DHEADER_NAME=$(NAME).h - < "heap.g" > "$(NAME).c"
