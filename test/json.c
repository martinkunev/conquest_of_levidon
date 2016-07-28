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

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <base.h>
#include <json.h>

#define SIZE 256
#define OFFSET 16

#define bytes_t(n) struct \
	{ \
		size_t size; \
		char data[n]; \
	}
#define bytes(value) {sizeof(value) - 1, value}
#define bytes_define(variable, value) bytes_t(sizeof(value) - 1) variable = bytes(value)

static void check_buffer_json(const unsigned char *restrict end, const unsigned char buffer[static restrict SIZE], const unsigned char *restrict result, size_t result_size, const unsigned char canary[static restrict SIZE])
{
	assert_ptr_equal(end, buffer + OFFSET + result_size);
	assert_memory_equal(buffer, canary, OFFSET);
	assert_memory_equal(buffer + OFFSET, result, result_size);
	assert_memory_equal(end, canary + OFFSET + result_size, SIZE - OFFSET - result_size);
}

static void test_json_parse(void **state)
{
	char canary[SIZE];
	char buffer[SIZE];
	char *end;

	// Use 10100101 as canary.
	memset(canary, '\xa5', SIZE);

	#define CASE(string) do \
		{ \
			union json *json = json_parse(string, sizeof(string) - 1); \
			memcpy(buffer, canary, SIZE); \
			end = json_dump(buffer + OFFSET, json); \
			check_buffer_json(end, buffer, string, sizeof(string) -1, canary); \
			json_free(json); \
		} while (0)

	CASE("\"hello\"");
	CASE("[]");
	CASE("{}");
	CASE("[null,1342,-42,17.35]");
	CASE("{\"key\":\"value\"}");

	// TODO unicode

	#undef CASE
}

static void test_json_dump(void **state)
{
	char canary[SIZE];
	char buffer[SIZE];
	char *end;

	union json *json;

	bytes_define(output, "{\"entries\":[5,0,11,-6,19.4,\"\",null,{\"abc\":\"alphabet\"}]}");

#define S(s) s, sizeof(s) - 1
	json = json_array();
	json = json_array_insert(json, json_integer(5));
	json = json_array_insert(json, json_integer(0));
	json = json_array_insert(json, json_integer(11));
	json = json_array_insert(json, json_integer(-6));
	json = json_array_insert(json, json_real(19.4));
	json = json_array_insert(json, json_string(S("")));
	json = json_array_insert(json, json_null());
	json = json_array_insert(json, json_object_insert(json_object(), S("abc"), json_string(S("alphabet"))));
	json = json_object_insert(json_object(), S("entries"), json);
#undef S

	assert_int_equal(json_size(json), output.size);

	// Use 10100101 as canary.
	memset(canary, '\xa5', SIZE);

	memcpy(buffer, canary, SIZE);
	end = json_dump(buffer + OFFSET, json);
	check_buffer_json(end, buffer, output.data, output.size, canary);

	// TODO unicode
}

#undef OFFSET
#undef SIZE

int main(void)
{
	const struct CMUnitTest tests[] =
	{
		cmocka_unit_test(test_json_parse),
		cmocka_unit_test(test_json_dump),
	};
	return cmocka_run_group_tests(tests, 0, 0);
}
