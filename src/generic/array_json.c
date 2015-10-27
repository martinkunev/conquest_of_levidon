 #include <stdlib.h>
 #include "array_json.h"
 int array_json_expand(struct array_json *restrict array, size_t count)
{
 if (array->capacity < count)
 {
  size_t capacity;
  union json * *buffer;
  capacity = (array->capacity * 2) | (!array->capacity * 8);
  while (capacity < count)
   capacity *= 2;
  buffer = realloc(array->data, capacity * sizeof(*array->data));
  if (!buffer) return -1;
  array->data = buffer;
  array->capacity = capacity;
 }
 return 0;
}
