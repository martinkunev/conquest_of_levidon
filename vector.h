struct vector
{
	void **data;
	size_t length, size;
};

bool vector_init(struct vector *restrict v);
#define vector_get(vector, index) ((vector)->data[index])
bool vector_add(struct vector *restrict v, void *value);
#define vector_term(vector) (free((vector)->data))
