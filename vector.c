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

/*void *array_resize(void *array, size_t count)
{
	//
}*/
