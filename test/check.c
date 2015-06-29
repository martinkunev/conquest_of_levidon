#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <base.h>

#include "format.h"
#include "json.h"

int main(void)
{
	const struct CMUnitTest tests[] =
	{
		cmocka_unit_test(test_format_bytes),
		cmocka_unit_test(test_format_byte),
		cmocka_unit_test(test_format_uint),
		cmocka_unit_test(test_format_int),
		cmocka_unit_test(test_format_base64),

		cmocka_unit_test(test_json_parse),
		cmocka_unit_test(test_json_dump),
	};
	return cmocka_run_group_tests(tests, 0, 0);
}
