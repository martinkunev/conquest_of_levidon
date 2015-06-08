#if defined(TEST)

#include <check.h>
#include <string.h>

#include <base.h>

#define SIZE 128
#define OFFSET 16

// http://check.sourceforge.net/doc/check_html/check_3.html

START_TEST(test_bytes)
{
	#define STRING "some string \n\xff\x00 !@#$%^&*()"

	char input[sizeof(STRING) - 1] = STRING;
	char expected[sizeof(input) + 2] = STRING "\x7f\x81";
	char output[sizeof(input) + 2];
	output[sizeof(input)] = '\x81';
	output[sizeof(input) + 1] = '\x81';

	#undef STRING

	*format_bytes(output, input, sizeof(input)) = '\x7f';

	ck_assert(!memcmp(output, expected, sizeof(expected)));
}
END_TEST

static void check_buffer(const unsigned char *restrict end, const unsigned char buffer[static restrict SIZE], const unsigned char *restrict result, size_t result_size, const unsigned char canary[static restrict SIZE])
{
	ck_assert(end == (buffer + OFFSET + result_size));
	ck_assert(!memcmp(buffer, canary, OFFSET));
	ck_assert(!memcmp(buffer + OFFSET, result, result_size));
	ck_assert(!memcmp(end, canary + OFFSET + result_size, SIZE - OFFSET - result_size));
}

START_TEST(test_byte)
{
	char canary[SIZE];
	char buffer[SIZE];
	char *end;

	char result[64] = {0};

	// Use 10100101 as canary.
	memset(canary, '\xa5', SIZE);

	/*memcpy(buffer, canary, SIZE);
	end = format_byte(buffer + OFFSET, '0', 0);
	ck_assert(end == (buffer + OFFSET));
	ck_assert(!memcmp(buffer, canary, SIZE));*/

	memcpy(buffer, canary, SIZE);
	end = format_byte(buffer + OFFSET, '@', 1);
	check_buffer(end, buffer, "@", 1, canary);

	memcpy(buffer, canary, SIZE);
	end = format_byte(buffer + OFFSET, '\xf0', 8);
	check_buffer(end, buffer, "\xf0\xf0\xf0\xf0\xf0\xf0\xf0\xf0", 8, canary);

	memcpy(buffer, canary, SIZE);
	end = format_byte(buffer + OFFSET, 'm', 11);
	check_buffer(end, buffer, "mmmmmmmmmmm", 11, canary);

	memcpy(buffer, canary, SIZE);
	end = format_byte(buffer + OFFSET, '\0', sizeof(result));
	check_buffer(end, buffer, result, sizeof(result), canary);
}
END_TEST

START_TEST(test_uint)
{
	char canary[SIZE];
	char buffer[SIZE];
	char *end;

	// Use 10100101 as canary.
	memset(canary, '\xa5', SIZE);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 0, 10);
	check_buffer(end, buffer, "0", 1, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 5, 10);
	check_buffer(end, buffer, "5", 1, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 371, 10);
	check_buffer(end, buffer, "371", 3, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 18446744073709551615ULL, 10);
	check_buffer(end, buffer, "18446744073709551615", 20, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 0xa5, 2);
	check_buffer(end, buffer, "10100101", 8, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 18446744073709551615ULL, 2);
	check_buffer(end, buffer, "11111111" "11111111" "11111111" "11111111" "11111111" "11111111" "11111111" "11111111", 64, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 05743, 8);
	check_buffer(end, buffer, "5743", 4, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 0x3fc, 16);
	check_buffer(end, buffer, "3fc", 3, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint_pad(buffer + OFFSET, 37, 10, 2, ' ');
	check_buffer(end, buffer, "37", 2, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint_pad(buffer + OFFSET, 37, 10, 4, ' ');
	check_buffer(end, buffer, "  37", 4, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint_pad(buffer + OFFSET, 37, 10, 4, '0');
	check_buffer(end, buffer, "0037", 4, canary);
}
END_TEST

START_TEST(test_int)
{
	char canary[SIZE];
	char buffer[SIZE];
	char *end;

	// Use 10100101 as canary.
	memset(canary, '\xa5', SIZE);

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, 0, 10);
	check_buffer(end, buffer, "0", 1, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, 5, 10);
	check_buffer(end, buffer, "5", 1, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, 371, 10);
	check_buffer(end, buffer, "371", 3, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, 9223372036854775807LL, 10);
	check_buffer(end, buffer, "9223372036854775807", 19, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, -1, 10);
	check_buffer(end, buffer, "-1", 2, canary);

	memcpy(buffer, canary, SIZE);
#if (INT64_MIN == -9223372036854775807LL - 1) /* 2's complement */
	end = format_int(buffer + OFFSET, -9223372036854775807LL - 1, 10);
	check_buffer(end, buffer, "-9223372036854775808", 20, canary);
#else /* ones' complement or sign-magnitude */
	end = format_int(buffer + OFFSET, -9223372036854775807LL, 10);
	check_buffer(end, buffer, "-9223372036854775807", 20, canary);
#endif

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, 0xa5, 2);
	check_buffer(end, buffer, "10100101", 8, canary);

	memcpy(buffer, canary, SIZE);
#if (INT64_MIN == -9223372036854775807LL - 1) /* 2's complement */
	end = format_int(buffer + OFFSET, -9223372036854775807LL - 1, 2);
	check_buffer(end, buffer, "-10000000" "00000000" "00000000" "00000000" "00000000" "00000000" "00000000" "00000000", 65, canary);
#else /* ones' complement or sign-magnitude */
	end = format_int(buffer + OFFSET, -9223372036854775807LL, 2);
	check_buffer(end, buffer, "-1111111" "11111111" "11111111" "11111111" "11111111" "11111111" "11111111" "11111111", 64, canary);
#endif

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, 05743, 8);
	check_buffer(end, buffer, "5743", 4, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, 0x3fc, 16);
	check_buffer(end, buffer, "3fc", 3, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int_pad(buffer + OFFSET, 37, 10, 2, ' ');
	check_buffer(end, buffer, "37", 2, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int_pad(buffer + OFFSET, 37, 10, 4, ' ');
	check_buffer(end, buffer, "  37", 4, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int_pad(buffer + OFFSET, 37, 10, 4, '0');
	check_buffer(end, buffer, "0037", 4, canary);
}
END_TEST

START_TEST(test_base64)
{
	char canary[SIZE];
	char buffer[SIZE];
	char *end;

	// Use 10100101 as canary.
	memset(canary, '\xa5', SIZE);

	#define CASE(input, output) do \
		{ \
			memcpy(buffer, canary, SIZE); \
			end = format_base64(buffer + OFFSET, input, sizeof(input) - 1); \
			check_buffer(end, buffer, output, sizeof(output) - 1, canary); \
		} while (0)

	CASE("", "");
	CASE("\x50", "k0");
	CASE("\x97\x4a", "BQE");
	CASE("\x30\x31\x32", "c34O");

	#undef CASE
}
END_TEST

int check_format(void)
{
	unsigned failed;

	Suite *s = suite_create("format");
	TCase *tc;

	tc = tcase_create("byte");
	tcase_add_test(tc, test_byte);
	suite_add_tcase(s, tc);

	tc = tcase_create("bytes");
	tcase_add_test(tc, test_bytes);
	suite_add_tcase(s, tc);

	tc = tcase_create("uint");
	tcase_add_test(tc, test_uint);
	suite_add_tcase(s, tc);

	tc = tcase_create("int");
	tcase_add_test(tc, test_int);
	suite_add_tcase(s, tc);

	tc = tcase_create("base64");
	tcase_add_test(tc, test_base64);
	suite_add_tcase(s, tc);

	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_VERBOSE);
	failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return failed;
}

#endif
