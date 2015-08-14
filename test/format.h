#include <format.h>

#define SIZE 128
#define OFFSET 16

static void test_format_bytes(void **state)
{
	#define STRING "some string \n\xff\x00 !@#$%^&*()"

	char input[sizeof(STRING) - 1] = STRING;
	char expected[sizeof(input) + 2] = STRING "\x7f\x81";
	char output[sizeof(input) + 2];
	output[sizeof(input)] = '\x81';
	output[sizeof(input) + 1] = '\x81';

	#undef STRING

	*format_bytes(output, input, sizeof(input)) = '\x7f';

	assert_memory_equal(output, expected, sizeof(expected));
}

static void check_buffer_format(const unsigned char *restrict end, const unsigned char buffer[static restrict SIZE], const unsigned char *restrict result, size_t result_size, const unsigned char canary[static restrict SIZE])
{
	assert_ptr_equal(end, buffer + OFFSET + result_size);
	assert_memory_equal(buffer, canary, OFFSET);
	assert_memory_equal(buffer + OFFSET, result, result_size);
	assert_memory_equal(end, canary + OFFSET + result_size, SIZE - OFFSET - result_size);
}

static void test_format_byte(void **state)
{
	char canary[SIZE];
	char buffer[SIZE];
	char *end;

	char result[64] = {0};

	// Use 10100101 as canary.
	memset(canary, '\xa5', SIZE);

	memcpy(buffer, canary, SIZE);
	end = format_byte(buffer + OFFSET, '@', 1);
	check_buffer_format(end, buffer, "@", 1, canary);

	memcpy(buffer, canary, SIZE);
	end = format_byte(buffer + OFFSET, '\xf0', 8);
	check_buffer_format(end, buffer, "\xf0\xf0\xf0\xf0\xf0\xf0\xf0\xf0", 8, canary);

	memcpy(buffer, canary, SIZE);
	end = format_byte(buffer + OFFSET, 'm', 11);
	check_buffer_format(end, buffer, "mmmmmmmmmmm", 11, canary);

	memcpy(buffer, canary, SIZE);
	end = format_byte(buffer + OFFSET, '\0', sizeof(result));
	check_buffer_format(end, buffer, result, sizeof(result), canary);
}

static void test_format_uint(void **state)
{
	char canary[SIZE];
	char buffer[SIZE];
	char *end;

	// Use 10100101 as canary.
	memset(canary, '\xa5', SIZE);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 0, 10);
	check_buffer_format(end, buffer, "0", 1, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 5, 10);
	check_buffer_format(end, buffer, "5", 1, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 371, 10);
	check_buffer_format(end, buffer, "371", 3, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 18446744073709551615ULL, 10);
	check_buffer_format(end, buffer, "18446744073709551615", 20, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 0xa5, 2);
	check_buffer_format(end, buffer, "10100101", 8, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 18446744073709551615ULL, 2);
	check_buffer_format(end, buffer, "11111111" "11111111" "11111111" "11111111" "11111111" "11111111" "11111111" "11111111", 64, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 05743, 8);
	check_buffer_format(end, buffer, "5743", 4, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint(buffer + OFFSET, 0x3fc, 16);
	check_buffer_format(end, buffer, "3fc", 3, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint_pad(buffer + OFFSET, 37, 10, 2, ' ');
	check_buffer_format(end, buffer, "37", 2, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint_pad(buffer + OFFSET, 37, 10, 4, ' ');
	check_buffer_format(end, buffer, "  37", 4, canary);

	memcpy(buffer, canary, SIZE);
	end = format_uint_pad(buffer + OFFSET, 37, 10, 4, '0');
	check_buffer_format(end, buffer, "0037", 4, canary);
}

static void test_format_int(void **state)
{
	char canary[SIZE];
	char buffer[SIZE];
	char *end;

	// Use 10100101 as canary.
	memset(canary, '\xa5', SIZE);

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, 0, 10);
	check_buffer_format(end, buffer, "0", 1, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, 5, 10);
	check_buffer_format(end, buffer, "5", 1, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, 371, 10);
	check_buffer_format(end, buffer, "371", 3, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, 9223372036854775807LL, 10);
	check_buffer_format(end, buffer, "9223372036854775807", 19, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, -1, 10);
	check_buffer_format(end, buffer, "-1", 2, canary);

	memcpy(buffer, canary, SIZE);
#if (INT64_MIN == -9223372036854775807LL - 1) /* 2's complement */
	end = format_int(buffer + OFFSET, -9223372036854775807LL - 1, 10);
	check_buffer_format(end, buffer, "-9223372036854775808", 20, canary);
#else /* ones' complement or sign-magnitude */
	end = format_int(buffer + OFFSET, -9223372036854775807LL, 10);
	check_buffer_format(end, buffer, "-9223372036854775807", 20, canary);
#endif

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, 0xa5, 2);
	check_buffer_format(end, buffer, "10100101", 8, canary);

	memcpy(buffer, canary, SIZE);
#if (INT64_MIN == -9223372036854775807LL - 1) /* 2's complement */
	end = format_int(buffer + OFFSET, -9223372036854775807LL - 1, 2);
	check_buffer_format(end, buffer, "-10000000" "00000000" "00000000" "00000000" "00000000" "00000000" "00000000" "00000000", 65, canary);
#else /* ones' complement or sign-magnitude */
	end = format_int(buffer + OFFSET, -9223372036854775807LL, 2);
	check_buffer_format(end, buffer, "-1111111" "11111111" "11111111" "11111111" "11111111" "11111111" "11111111" "11111111", 64, canary);
#endif

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, 05743, 8);
	check_buffer_format(end, buffer, "5743", 4, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int(buffer + OFFSET, 0x3fc, 16);
	check_buffer_format(end, buffer, "3fc", 3, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int_pad(buffer + OFFSET, 37, 10, 2, ' ');
	check_buffer_format(end, buffer, "37", 2, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int_pad(buffer + OFFSET, 37, 10, 4, ' ');
	check_buffer_format(end, buffer, "  37", 4, canary);

	memcpy(buffer, canary, SIZE);
	end = format_int_pad(buffer + OFFSET, 37, 10, 4, '0');
	check_buffer_format(end, buffer, "0037", 4, canary);
}

static void test_format_base64(void **state)
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
			check_buffer_format(end, buffer, output, sizeof(output) - 1, canary); \
		} while (0)

	CASE("", "");
	CASE("\x50", "k0");
	CASE("\x97\x4a", "BQE");
	CASE("\x30\x31\x32", "c34O");

	#undef CASE
}

#undef OFFSET
#undef SIZE
