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

#include <p.h>

static void test_wall_blocks(void **state)
{
	float left = 3, right = 7, top = 5, bottom = 4;

	assert_false(wall_blocks((struct position){4, 1}, (struct position){4, 3}, left, right, top, bottom));
	assert_false(wall_blocks((struct position){4, 7}, (struct position){4, 5}, left, right, top, bottom));
	assert_true(wall_blocks((struct position){4, 7}, (struct position){4, 4}, left, right, top, bottom));
	assert_true(wall_blocks((struct position){4, 1}, (struct position){4, 5}, left, right, top, bottom));
}

static void test_pawn_blocks(void **state)
{
	struct position start = {1, 3};
	struct position end = {2, 5};
	struct position pawn0 = {3, 6};
	struct position pawn1 = {2, 4};

	assert_false(pawn_blocks(start, end, pawn0));
	assert_true(pawn_blocks(start, end, pawn1));
}

int main(void)
{
	const struct CMUnitTest tests[] =
	{
		cmocka_unit_test(test_wall_blocks),
		cmocka_unit_test(test_pawn_blocks),
	};
	return cmocka_run_group_tests(tests, 0, 0);
}
