#if !defined(array_suffix)
# define array_suffix
#endif

#if !defined(array_type)
# define array_type void *
#endif

#define CAT2(a0, a1) a0 ## a1
#define NAME2(a0, a1) CAT2(a0, a1)

#define CAT3(a0, a1, a2) a0 ## a1 ## a2
#define NAME3(a0, a1, a2) CAT3(a0, a1, a2)

struct NAME2(array, array_suffix)
{
	size_t count;
	size_t data_count;
	array_type *data;
};

int NAME3(array, array_suffix, _init)(struct NAME2(array, array_suffix) *restrict array, size_t data_count);

int NAME3(array, array_suffix, _expand)(struct NAME2(array, array_suffix) *restrict array, size_t count);
int NAME3(array, array_suffix, _push)(struct NAME2(array, array_suffix) *restrict array, array_type value);

static inline void NAME3(array, array_suffix, _term)(struct NAME2(array, array_suffix) *restrict array)
{
	free(array->data);
}

#define ARRAY_SIZE_DEFAULT 32
