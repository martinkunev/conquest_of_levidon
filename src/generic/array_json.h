struct array_json
{
 size_t count;
 size_t count_allocated;
 union json * *data;
};
static inline void array_json_term(struct array_json *restrict array)
{
 free(array->data);
}
 int array_json_expand(struct array_json *restrict array, size_t count);
