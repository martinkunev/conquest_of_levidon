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

#include <pathfinding.c>

static void test_obstacle_blocks(void **state)
{
	struct obstacle obstacle = {3, 7, 5, 4};

	assert_false(path_blocked_obstacle((struct position){4, 1}, (struct position){4, 3}, &obstacle));
	assert_false(path_blocked_obstacle((struct position){4, 7}, (struct position){4, 5}, &obstacle));
	assert_true(path_blocked_obstacle((struct position){4, 7}, (struct position){4, 4}, &obstacle));
	assert_true(path_blocked_obstacle((struct position){4, 1}, (struct position){4, 5}, &obstacle));
}

static void test_pawn_blocks(void **state)
{
	struct position start = {1, 3};
	struct position end = {2, 5};
	struct position pawn0 = {3, 6};
	struct position pawn1 = {2, 4};

	assert_false(path_blocked_pawn(start, end, pawn0));
	assert_true(path_blocked_pawn(start, end, pawn1));
}

int main(void)
{
	const struct CMUnitTest tests[] =
	{
		cmocka_unit_test(test_obstacle_blocks),
		cmocka_unit_test(test_pawn_blocks),
	};
	return cmocka_run_group_tests(tests, 0, 0);
}
