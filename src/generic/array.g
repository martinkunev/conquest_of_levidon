// The arguments with which the macro functions are called are guaranteed to produce no side effects.

#if !defined(array_name)
# define array_name array
#endif

#if !defined(array_type)
# define array_type void *
#endif

#define NAME_CAT_EXPAND(a, b) a ## b
#define NAME_CAT(a, b) NAME_CAT_EXPAND(a, b)
#define NAME(suffix) NAME_CAT(array_name, suffix)

#define STRING_EXPAND(string) #string
#define STRING(string) STRING_EXPAND(string)

#if defined(HEADER) || defined(SOURCE)
# define STATIC
#else
# define STATIC static
#endif

#if !defined(SOURCE)
struct array_name
{
	size_t count;
	size_t capacity;
	array_type *data;
};

static inline void NAME(_term)(struct array_name *restrict array)
{
	free(array->data);
}
STATIC int NAME(_expand)(struct array_name *restrict array, size_t count);
#endif

#if !defined(HEADER)

#if defined(SOURCE)
# define INCLUDE0 #include <stdlib.h>
INCLUDE0
# define INCLUDE1 #include STRING(HEADER_NAME)
INCLUDE1
#else
# include <stdlib.h>
#endif

#define ARRAY_SIZE_BASE 8

STATIC int NAME(_expand)(struct array_name *restrict array, size_t count)
{
	if (array->capacity < count)
	{
		size_t capacity;
		array_type *buffer;

		// Round count up to the next power of 2 that is >= ARRAY_SIZE_BASE.
		capacity = (array->capacity * 2) | (!array->capacity * ARRAY_SIZE_BASE);
		while (capacity < count)
			capacity *= 2;

		buffer = realloc(array->data, capacity * sizeof(*array->data));
		if (!buffer) return -1;
		array->data = buffer;
		array->capacity = capacity;
	}
	return 0;
}

#endif /* !defined(HEADER) */

#undef STATIC

#undef STRING
#undef STRING_EXPAND

#undef NAME
#undef NAME_CAT
#undef NAME_CAT_EXPAND

#undef array_type
#undef array_name
