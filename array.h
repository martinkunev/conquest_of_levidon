#if !defined array_type
# define array_type void *
#endif

struct array
{
	size_t count;
	size_t data_count;
	array_type *data;
};

int array_init(struct array *restrict array, size_t data_count);

int array_expand(struct array *restrict array, size_t count);
int array_push(struct array *restrict array, array_type value);

static inline void array_term(struct array *restrict array)
{
	free(array->data);
}

enum {ARRAY_SIZE_DEFAULT = 32};
