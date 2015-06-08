#if defined(TEST)

#include <check.h>

#include <base.h>

#define SIZE 256
#define OFFSET 16

static void check_buffer(const unsigned char *restrict end, const unsigned char buffer[static restrict SIZE], const unsigned char *restrict result, size_t result_size, const unsigned char canary[static restrict SIZE])
{
	ck_assert(end == (buffer + OFFSET + result_size));
	ck_assert(!memcmp(buffer, canary, OFFSET));
	ck_assert(!memcmp(buffer + OFFSET, result, result_size));
	ck_assert(!memcmp(end, canary + OFFSET + result_size, SIZE - OFFSET - result_size));
}

START_TEST(test_parse)
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
			check_buffer(end, buffer, string, sizeof(string) -1, canary); \
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
END_TEST

START_TEST(test_dump)
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

	ck_assert(json_size(json) == output.size);

	// Use 10100101 as canary.
	memset(canary, '\xa5', SIZE);

	memcpy(buffer, canary, SIZE);
	end = json_dump(buffer + OFFSET, json);
	check_buffer(end, buffer, output.data, output.size, canary);

	// TODO unicode
}
END_TEST

int check_json(void)
{
	unsigned failed;

	Suite *s = suite_create("json");
	TCase *tc;

	tc = tcase_create("parse");
	tcase_add_test(tc, test_parse);
	suite_add_tcase(s, tc);

	tc = tcase_create("dump");
	tcase_add_test(tc, test_dump);
	suite_add_tcase(s, tc);

	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_VERBOSE);
	failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return failed;
}

#endif
