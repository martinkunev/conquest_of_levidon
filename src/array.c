#include <stdlib.h>

#define CAT2(a0, a1) a0 ## a1
#define NAME2(a0, a1) CAT2(a0, a1)

#define CAT3(a0, a1, a2) a0 ## a1 ## a2
#define NAME3(a0, a1, a2) CAT3(a0, a1, a2)

int NAME3(array, array_suffix, _init)(struct NAME2(array, array_suffix) *restrict array, size_t data_count)
{
	array->count = 0;
	array->data_count = data_count;
	array->data = malloc(data_count * sizeof(*array->data));
	if (!array->data) return ERROR_MEMORY;

	return 0;
}

int NAME3(array, array_suffix, _expand)(struct NAME2(array, array_suffix) *restrict array, size_t count)
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

int NAME3(array, array_suffix, _push)(struct NAME2(array, array_suffix) *restrict array, array_type value)
{
	int status = NAME3(array, array_suffix, _expand)(array, array->count + 1);
	if (status) return status;
	array->data[array->count] = value;
	array->count += 1;
	return 0;
}
