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

#include <map.h>

void __wrap_free(void *ptr)
{
	check_expected(ptr);
}

enum {NEUTRAL, SELF, ALLY, ENEMY};
static struct player players[] =
{
	[NEUTRAL] = {.alliance = 0},
	[SELF] = {.alliance = 1},
	[ALLY] = {.alliance = 1},
	[ENEMY] = {.alliance = 2},
};

static void region_turn_process_empty(void **state)
{
	struct game game = {.players = players, .players_count = sizeof(players) / sizeof(*players)};

	struct region region = {0};
	region.owner = 1;
	region.garrison.owner = 1;
	region.garrison.siege = 0;
	region.troops = 0;
	region.built = 0;

	region_turn_process(&game, &region);
	assert_int_equal(region.owner, 1);
	assert_int_equal(region.garrison.owner, 1);
	assert_int_equal(region.garrison.siege, 0);
}

static void region_turn_process_self(void **state)
{
	struct game game = {.players = players, .players_count = sizeof(players) / sizeof(*players)};

	struct region region = {0}, region_other = {0};
	struct troop troops[] =
	{
		{.owner = SELF, .location = &region_other, .move = &region, ._next = 0},
	};
	region.owner = SELF;
	region.garrison.owner = SELF;
	region.garrison.siege = 0;
	region.troops = troops;
	region.built = 0;

	region_turn_process(&game, &region);
	assert_int_equal(region.owner, SELF);
	assert_int_equal(region.garrison.owner, SELF);
	assert_int_equal(region.garrison.siege, 0);
}

static void region_turn_process_ally(void **state)
{
	struct game game = {.players = players, .players_count = sizeof(players) / sizeof(*players)};

	struct region region = {0}, region_other = {0};
	struct troop troops[] =
	{
		{.owner = ALLY, .location = &region_other, .move = &region, ._next = 0},
	};
	region.owner = SELF;
	region.garrison.owner = SELF;
	region.garrison.siege = 0;
	region.troops = troops;
	region.built = 0;

	region_turn_process(&game, &region);
	assert_int_equal(region.owner, SELF);
	assert_int_equal(region.garrison.owner, SELF);
	assert_int_equal(region.garrison.siege, 0);
}

static void region_turn_process_enemy(void **state)
{
	struct game game = {.players = players, .players_count = sizeof(players) / sizeof(*players)};

	struct region region = {0}, region_other = {0};
	struct troop troops[] =
	{
		{.owner = ENEMY, .location = &region_other, .move = &region, ._next = 0},
	};
	region.owner = SELF;
	region.garrison.owner = SELF;
	region.garrison.siege = 0;
	region.troops = troops;
	region.built = 0;

	region_turn_process(&game, &region);
	assert_int_equal(region.owner, ENEMY);
	assert_int_equal(region.garrison.owner, ENEMY);
	assert_int_equal(region.garrison.siege, 0);
}

static void region_turn_process_liberate(void **state)
{
	struct game game = {.players = players, .players_count = sizeof(players) / sizeof(*players)};

	struct region region = {0}, region_other = {0};
	struct troop troops[] =
	{
		{.owner = SELF, .location = LOCATION_GARRISON, .move = LOCATION_GARRISON, ._next = troops + 1},
		{.owner = ALLY, .location = &region_other, .move = &region, ._next = 0},
	};
	region.owner = ENEMY;
	region.garrison.owner = SELF;
	region.garrison.siege = 1;
	region.troops = troops;
	region.built = (1 << BuildingPalisade);

	region_turn_process(&game, &region);
	assert_int_equal(region.owner, SELF);
	assert_int_equal(region.garrison.owner, SELF);
	assert_int_equal(region.garrison.siege, 0);
}

static void region_turn_process_siege(void **state)
{
	struct game game = {.players = players, .players_count = sizeof(players) / sizeof(*players)};

	struct region region = {0}, region_other = {0};
	struct troop troops[] =
	{
		{.owner = SELF, .location = LOCATION_GARRISON, .move = LOCATION_GARRISON, ._next = troops + 1},
		{.owner = ENEMY, .location = &region_other, .move = &region, ._next = 0},
	};
	region.owner = ENEMY;
	region.garrison.owner = SELF;
	region.garrison.siege = 0;
	region.troops = troops;
	region.built = (1 << BuildingPalisade);

	region_turn_process(&game, &region);
	assert_int_equal(region.owner, ENEMY);
	assert_int_equal(region.garrison.owner, SELF);
	assert_int_equal(region.garrison.siege, 1);
}

static void region_turn_process_garrison_conquer(void **state)
{
	struct game game = {.players = players, .players_count = sizeof(players) / sizeof(*players)};

	struct region region = {0}, region_other = {0};
	struct troop troops[] =
	{
		{.owner = SELF, .location = LOCATION_GARRISON, .move = LOCATION_GARRISON, ._next = troops + 1},
		{.owner = ENEMY, .location = &region_other, .move = &region, ._next = 0},
	};
	region.owner = ENEMY;
	region.garrison.owner = SELF;
	region.garrison.siege = 2;
	region.troops = troops;
	region.built = (1 << BuildingPalisade);

	expect_value(__wrap_free, ptr, region.troops);

	region_turn_process(&game, &region);
	assert_int_equal(region.owner, ENEMY);
	assert_int_equal(region.garrison.owner, ENEMY);
	assert_int_equal(region.garrison.siege, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] =
	{
		cmocka_unit_test(region_turn_process_empty),
		cmocka_unit_test(region_turn_process_self),
		cmocka_unit_test(region_turn_process_ally),
		cmocka_unit_test(region_turn_process_enemy),
		cmocka_unit_test(region_turn_process_liberate),
		cmocka_unit_test(region_turn_process_siege),
		cmocka_unit_test(region_turn_process_garrison_conquer),
	};
	return cmocka_run_group_tests(tests, 0, 0);
}
