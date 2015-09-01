 #include <stdlib.h>
 #include "array_json.h"
 int array_json_init(struct array_json *restrict array, size_t count_allocated)
{
 array->count = 0;
 array->count_allocated = count_allocated;
 array->data = malloc(count_allocated * sizeof(*array->data));
 if (!array->data) return -1;
 return 0;
}
 int array_json_expand(struct array_json *restrict array, size_t count)
{
 if (array->count_allocated < count)
 {
  size_t count_allocated;
  union json * *buffer;
  for(count_allocated = array->count_allocated * 2; count_allocated < count; count_allocated *= 2)
   ;
  buffer = realloc(array->data, count_allocated * sizeof(*array->data));
  if (!buffer) return -1;
  array->data = buffer;
  array->count_allocated = count_allocated;
 }
 return 0;
}