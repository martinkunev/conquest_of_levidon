#ifndef _TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "errors.h"

/* String */

// TODO: consider using BUFSIZ here
#define BLOCK_SIZE 4096

// String literal. Contains length and pointer to the data. The data is usually NUL-terminated so that it can be passed to standard functions without modification.
struct string
{
	char *data;
	size_t length;
};

// Generates string literal from data and length. If no length is passed, it assumes that string data is static array and determines its length with sizeof.
// Examples:
//  struct string name = string(name_data, name_length);
//  struct string key = string("uuid");
#define string_(data, length, ...) (struct string){(data), (length)}
#define string(...) string_(__VA_ARGS__, sizeof(__VA_ARGS__) - 1)

/* Vector */

// The pointer to the data is the first member of the struct so that pointer to a vector can be used directly by casting it to a pointer to the stored type.
struct vector
{
	void **data;
	size_t length;
	size_t size;
};

#define VECTOR_EMPTY ((struct vector){0})

#define vector_get(vector, index) ((vector)->data[index])
int vector_add(struct vector *restrict v, void *value);
int vector_resize(struct vector *restrict vector, size_t size);
#define vector_term(vector) (free((vector)->data))

/* Dictionary */

#define DICT_SIZE_BASE 16

struct dict
{
	struct dict_item
	{
		size_t key_size;
		const char *key_data;
		void *value;
		struct dict_item *_next;
	} **items;
	size_t count, size;
};

struct dict_iterator
{
	size_t index;
	struct dict_item *item;
};

// Initializes dictionary iterator and returns the first item
const struct dict_item *dict_first(struct dict_iterator *restrict it, const struct dict *d);
// Returns next item in a dictionary iterator
const struct dict_item *dict_next(struct dict_iterator *restrict it, const struct dict *d);

// WARNING: size must be a power of 2
bool dict_init(struct dict *restrict dict, size_t size);

int dict_set(struct dict *restrict dict, const struct string *key, void *value, void **result);
#define dict_add(dict, key, value) dict_set((dict), (key), (value), 0)

void *dict_get(const struct dict *dict, const struct string *key);

void *dict_remove(struct dict *restrict dict, const struct string *key);

void dict_term(struct dict *restrict dict);

#define _TYPES_H
#endif
