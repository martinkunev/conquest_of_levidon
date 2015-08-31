struct array_json
{
 size_t count;
 size_t count_allocated;
 union json * *data;
};
 int array_json_init(struct array_json *restrict array, size_t count_allocated);
static inline void array_json_term(struct array_json *restrict array)
{
 free(array->data);
}
 int array_json_expand(struct array_json *restrict array, size_t count);
