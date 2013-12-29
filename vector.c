#include <stdbool.h>
#include <stdlib.h>

#include "types.h"

#define VECTOR_SIZE_BASE 4

bool vector_init(struct vector *restrict v)
{
	v->length = 0;
	v->size = VECTOR_SIZE_BASE;

	v->data = malloc(sizeof(void *) * VECTOR_SIZE_BASE);
	if (!v->data) return false; // memory error
	return v;
}

bool vector_add(struct vector *restrict v, void *value)
{
	if (v->length == v->size)
	{
		void **buffer = realloc(v->data, sizeof(void *) * (v->size * 2));
		if (!buffer) return 0; // memory error
		v->size *= 2;
		v->data = buffer;
	}
	return (v->data[v->length++] = value);
}
