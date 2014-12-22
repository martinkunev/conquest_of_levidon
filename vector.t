// #define vector_type int

// #define vector_name vector

#include <stdlib.h>

#include "errors.h"

#define VECTOR_SIZE_BASE 8

#define VECTOR_NAME_EXPAND_(name, suffix) name##_##suffix
#define VECTOR_NAME_EXPAND(name, suffix) VECTOR_NAME_EXPAND_(name, suffix)
#define VECTOR_NAME(suffix) VECTOR_NAME_EXPAND(vector_name, suffix)

// The first member is data so that a pointer to vector can be casted and used directly as pointer to vector_type.
struct vector_name
{
	vector_type *data;
	size_t length;
	size_t size;
};

static vector_type *VECTOR_NAME(insert)(struct vector_name *vector)
{
	if (vector->length == vector->size)
	{
		vector_type *buffer;
		vector->size = (vector->size ? (vector->size * 2) : VECTOR_SIZE_BASE);
		buffer = realloc(vector->data, vector->size * sizeof(*vector->data));
		if (!buffer) return 0; // memory error
		vector->data = buffer;
	}
	return vector->data + vector->length++;
}

static int VECTOR_NAME(add)(struct vector_name *restrict vector, vector_type value)
{
	if (vector->length == vector->size)
	{
		vector_type *buffer;
		vector->size = (vector->size ? (vector->size * 2) : VECTOR_SIZE_BASE);
		buffer = realloc(vector->data, vector->size * sizeof(*vector->data));
		if (!buffer) return ERROR_MEMORY; // memory error
		vector->data = buffer;
	}
	vector->data[vector->length++] = value;
	return 0;
}

static void VECTOR_NAME(shrink)(struct vector_name *vector)
{
	vector->size = vector->length;
	vector->data = realloc(vector->data, vector->size * sizeof(*vector->data));
}

#define vector_term(vector) (free((vector)->data))
