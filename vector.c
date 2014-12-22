#include <stdlib.h>
#include <sys/types.h>

#include "types.h"

#define VECTOR_SIZE_BASE 8

int vector_add(struct vector *restrict v, void *value)
{
	if (v->length == v->size)
	{
		void **buffer;
		v->size = (v->size ? v->size * 2 : VECTOR_SIZE_BASE);
		buffer = realloc(v->data, sizeof(void *) * v->size);
		if (!buffer) return ERROR_MEMORY; // not enough memory; operation canceled
		v->data = buffer;
	}
	v->data[v->length++] = value;
	return 0;
}

int vector_resize(struct vector *restrict vector, size_t size)
{
	if (vector->size < size)
	{
		void **buffer;

		// Round size up to a power of 2.
		do vector->size *= 2;
		while (vector->size < size);

		buffer = realloc(vector->data, vector->size * sizeof(*buffer));
		if (!buffer) return ERROR_MEMORY; // memory error
		vector->data = buffer;
	}
	return 0;
}
