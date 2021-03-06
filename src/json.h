/*
 * Conquest of Levidon
 * Copyright (C) 2016  Martin Kunev <martinkunev@gmail.com>
 *
 * This file is part of Conquest of Levidon.
 *
 * Conquest of Levidon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 3 of the License.
 *
 * Conquest of Levidon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Conquest of Levidon.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>

union json;

#include "generic/array_json.h"

#define hashmap_type union json *
#include "hashmap.h"

#define JSON_DEPTH_MAX 7

enum json_type {JSON_NULL, JSON_BOOLEAN, JSON_INTEGER, JSON_REAL, JSON_STRING, JSON_ARRAY, JSON_OBJECT};

// WARNING: union json should only be allocated internally; operate only with pointers allocated by the library

union json
{
	bool boolean;
	long long integer;
	double real;
	struct string
	{
		size_t size;
		char *data;
	} string;
	struct array_json array;
	struct hashmap object;
};
struct json_internal
{
	union json data;
	enum json_type type;
};

static inline enum json_type json_type(const union json *json)
{
	struct json_internal *internal = (struct json_internal *)json;
	return internal->type;
}

union json *json_null(void);
union json *json_boolean(bool value);
union json *json_integer(long long value);
union json *json_real(double value);
union json *json_string(const char *data, size_t size);
union json *json_array(void);
union json *json_object(void);

union json *json_array_insert(union json *restrict container, union json *restrict value);
union json *json_object_insert(union json *restrict container, const unsigned char *restrict key_data, size_t key_size, union json *restrict value);

union json *json_parse(const unsigned char *data, size_t size);
union json *json_clone(const union json *json);

ssize_t json_string_size(const char *restrict data, size_t size);
ssize_t json_size(const union json *restrict json);

char *json_string_dump(unsigned char *restrict dest, const unsigned char *restrict src, size_t size);
char *json_dump(char *restrict result, const union json *restrict json);

void json_free(union json *restrict json);
