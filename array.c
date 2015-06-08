#include <stdlib.h>

int array_init(struct array *restrict array, size_t data_count)
{
	array->count = 0;
	array->data_count = data_count;
	array->data = malloc(data_count * sizeof(*array->data));
	if (!array->data) return ERROR_MEMORY;

	return 0;
}

int array_expand(struct array *restrict array, size_t count)
{
	if (array->data_count < count)
	{
		size_t data_count;
		array_type *buffer;

		// Round count up to the next power of 2.
		for(data_count = array->data_count * 2; data_count < count; data_count *= 2)
			;

		buffer = realloc(array->data, data_count * sizeof(*array->data));
		if (!buffer) return ERROR_MEMORY;
		array->data = buffer;
		array->data_count = data_count;
	}
	return 0;
}

int array_push(struct array *restrict array, array_type value)
{
	int status = array_expand(array, array->count + 1);
	if (status) return status;
	array->data[array->count] = value;
	array->count += 1;
	return 0;
}
