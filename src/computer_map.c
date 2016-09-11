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

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "log.h"
#include "game.h"
#include "draw.h"
#include "map.h"
#include "resources.h"
#include "computer.h"
#include "computer_map.h"

#define MAP_COMMAND_PRIORITY_THRESHOLD 0.5 /* TODO maybe this should be a function */
#define TROOPS_EXPECTED 2 /* TODO this is completely arbitrary */
#define SPEED_FAST 7

struct region_troop
{
	struct region *region;
	struct troop *troop;
};

#define array_type struct region_troop
#define array_name array_troops
#include "generic/array.g"

struct region_order
{
	struct region *region;
	enum {ORDER_BUILD, ORDER_TRAIN} type;
	union
	{
		uint32_t building;
		size_t unit;
	} target;
	double priority;
};

#define array_type struct region_order
#define array_name array_orders
#include "generic/array.g"

#define heap_type struct region_order *
#define heap_name heap_orders
#define heap_above(a, b) ((a)->priority >= (b)->priority)
#include "generic/heap.g"

// TODO remember an estimate for the strength of each region; keep the estimate for the entire game; update the estimate when the region is visible

enum {UNIT_RANGED = 0x1, UNIT_ASSAULT = 0x2, UNIT_FAST = 0x4, UNIT_VANGUARD = 0x8};

struct troop_info
{
	unsigned ranged;
	unsigned assault;
	unsigned fast;
	unsigned vanguard;
	unsigned total;
};

struct region_info
{
	double importance;
	unsigned neighbors;
	unsigned neighbors_neutral, neighbors_enemy;
	unsigned neighbors_unknown;

	struct
	{
		double self, ally;
	} strength;
	struct
	{
		double self, ally;
	} strength_garrison; // garrison or assault strength
	double strength_enemy;
	double strength_enemy_neighbors;

	const struct garrison_info *restrict garrison;
	unsigned garrisons_enemy;

	struct troop_info troops;

	struct troop_info troops_needed;

	bool nearby; // whether the player can reach the region in one turn
};

struct context
{
	double importance_expected;
	unsigned count_expected;
	unsigned char regions_visible[REGIONS_LIMIT];
};

struct survivors
{
	double region;
	double region_garrison;
};

static void distances_compute(const struct game *restrict game, unsigned char regions_distances[static restrict REGIONS_LIMIT][REGIONS_LIMIT])
{
	size_t i, j, k;

	// Set distances between neighbors.
	for(i = 0; i < game->regions_count; ++i)
	{
		for(j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			const struct region *restrict neighbor = game->regions[i].neighbors[j];
			if (!neighbor)
				continue;
			regions_distances[i][neighbor->index] = 1;
		}
	}

	// Find shortest distance between any pair of regions.
	for(i = 0; i < game->regions_count; ++i)
	{
		for(j = 0; j < game->regions_count; ++j)
		{
			if (i == j)
				continue;
			for(k = 0; k < game->regions_count; ++k)
			{
				unsigned distance;
				if ((i == k) || (j == k))
					continue;
				distance = regions_distances[j][i] + regions_distances[i][k];
				if (!regions_distances[j][k] || (regions_distances[j][k] > distance))
					regions_distances[j][k] = distance;
			}
		}
	}
}

// TODO use a single function for this and the income logic in main.c
static void income_calculate(const struct game *restrict game, struct resources *restrict result, unsigned char player)
{
	*result = (struct resources){0};

	for(size_t index = 0; index < game->regions_count; ++index)
	{
		const struct region *region = game->regions + index;
		const struct troop *restrict troop;

		for(troop = region->troops; troop; troop = troop->_next)
		{
			const struct region *restrict destination;

			if (troop->owner != player)
				continue;

			destination = ((troop->move == LOCATION_GARRISON) ? region : troop->move);

			if ((troop->owner == destination->owner) && (destination->owner == destination->garrison.owner))
			{
				// Troops expenses are covered by the move region.
				if (troop->move == LOCATION_GARRISON)
					continue;
				resource_add(result, &troop->unit->support);
			}
			else
			{
				// Troop expenses are covered by another region. Double expenses.
				struct resources expense;
				resource_multiply(&expense, &troop->unit->support, 2);
				resource_add(result, &expense);
			}
		}

		// Add region income if the garrison is not under siege.
		if ((region->owner == player) && (region->owner == region->garrison.owner))
		{
			struct resources income_region = {0};
			region_income(region, &income_region);
			resource_add(result, &income_region);
		}
	}
}

static unsigned unit_class(const struct unit *restrict unit)
{
	unsigned class = 0;

	if (unit->ranged.weapon)
	{
		// TODO what if the unit is more useful as melee than as ranged
		class |= UNIT_RANGED;
		if (unit->ranged.weapon == WEAPON_BLUNT)
			class |= UNIT_ASSAULT;
	}
	else if (unit->melee.weapon == WEAPON_BLUNT)
	{
		class |= UNIT_ASSAULT;
	}

	if (unit->speed >= SPEED_FAST)
		class |= UNIT_FAST;

	if (!class)
		class |= UNIT_VANGUARD;

	return class;
}

static double building_value(const struct region_info *restrict region_info, const struct troop_info *restrict troops_info, const struct resources *restrict income, bool income_shortage, size_t index)
{
	double value;

	unsigned neighbors_dangerous = region_info->neighbors_unknown + region_info->neighbors_enemy;

	switch (index)
	{
	case BuildingWatchTower:
		value = desire_buildings[index];
		if (region_info->neighbors_unknown) value += (double)region_info->neighbors_unknown / region_info->neighbors;
		value *= region_info->importance / 1000.0;
		break;

	case BuildingFarm:
	case BuildingIrrigation:
	case BuildingSawmill:
	case BuildingMine:
	case BuildingBloomery:
		value = desire_buildings[index];

		// Check if the given building resolves a resource shortage.
		// TODO this will not work if there is shortage of several resources and the building resolves just one of them
		if (income_shortage && resource_enough(income, &BUILDINGS[index].support))
			value *= 2;

		// Check if the region is secure.
		if (neighbors_dangerous)
			value *= 0.25;

		break;

	case BuildingPalisade:
	case BuildingFortress:
		//value = region_info->troops.ranged * desire_buildings[index];
		value = region_info->importance / 1000.0;
		value *= (double)neighbors_dangerous / region_info->neighbors;
		break;

	default:
		{
			const struct unit *units[] = {
				[BuildingBarracks] = UNITS + UnitMilitia,
				[BuildingArcheryRange] = UNITS + UnitArcher,
				[BuildingForge] = UNITS + UnitPikeman,
				[BuildingStables] = UNITS + UnitLightCavalry,
				[BuildingWorkshop] = UNITS + UnitBatteringRam,
			};

			unsigned class;

			assert(index < sizeof(units) / sizeof(*units));
			class = unit_class(units[index]);

			if (troops_info->total)
			{
				if (class & UNIT_VANGUARD)
					value = (double)troops_info->vanguard / troops_info->total;
				else if (class & UNIT_ASSAULT)
					value = (double)troops_info->assault / troops_info->total;
				else if (class & UNIT_RANGED)
					value = (double)troops_info->ranged / troops_info->total;
				else
				{
					assert(class & UNIT_FAST);
					value = (double)troops_info->fast / troops_info->total;
				}
			}
			else value = 0.0;

			// Avoid construction of troop buildings in threatened regions.
			if (neighbors_dangerous)
				value *= 0.5;
		}

		break;
	}

	return value;
}

static double troop_value(const struct context *restrict context, const struct region_info *restrict region_info, const struct troop_info *restrict troops_info, const struct resources *restrict income, bool income_shortage, size_t index)
{
	unsigned class = unit_class(UNITS + index);
	double value = unit_importance(UNITS + index, 0) * UNITS[index].troops_count / (context->importance_expected * expense_significance(&UNITS[index].cost));

	// TODO improve this logic

	if (troops_info->total)
	{
		if (class & UNIT_VANGUARD)
			value *= (double)troops_info->vanguard / troops_info->total;
		else if (class & UNIT_ASSAULT)
			value *= (double)troops_info->assault / troops_info->total;
		else if (class & UNIT_RANGED)
			value *= (double)troops_info->ranged / troops_info->total;
		else
		{
			assert(class & UNIT_FAST);
			value *= (double)troops_info->fast / troops_info->total;
		}
	}

	// Ranged troops are necessary for assault defenses and attack.
	// Ideally, a proportion of the troops must be ranged.

	// Assault troops are necessary if there are enemy garrisons nearby.
	// The value of training an assault machine is larger if there are not enough assault machines.

	// Vanguard troops are necessary to protect ranged troops.

	// Fast troops are necessary for assault and against ranged troops.

	return value;
}

static int computer_map_orders_list(struct array_orders *restrict orders, const struct game *restrict game, unsigned char player, const struct context *restrict context, const struct region_info *restrict regions_info, const struct troop_info *restrict troops_info, const struct resources *restrict income)
{
	size_t i, j;

	bool income_shortage = ((income->gold < 0) || (income->food < 0) || (income->wood < 0) || (income->iron < 0) || (income->stone < 0));

	// Make a list of the orders available for the player.
	*orders = (struct array_orders){0};

	//printf(">>> start <<<\n");

	for(i = 0; i < game->regions_count; ++i)
	{
		struct region *restrict region = game->regions + i;

		if ((region->owner != player) || (region->garrison.owner != player))
			continue;

		// Don't check if there are enough resources and enough income, as the first performed order will invalidate the checks.
		// The checks will be done before and if the order is executed.

		if (region->construct < 0) // no construction in progress
		{
			for(j = 0; j < BUILDINGS_COUNT; ++j)
			{
				double priority;

				if (region_built(region, j) || !region_building_available(region, BUILDINGS + j))
					continue;

				priority = building_value(regions_info + i, troops_info, income, income_shortage, j);
				//LOG_DEBUG("%.*s | %.*s | %f", (int)region->name_length, region->name, (int)BUILDINGS[j].name_length, BUILDINGS[j].name, priority);
				if (priority < MAP_COMMAND_PRIORITY_THRESHOLD)
					continue;

				if (array_orders_expand(orders, orders->count + 1) < 0)
					goto error;
				orders->data[orders->count].priority = priority;
				orders->data[orders->count].region = region;
				orders->data[orders->count].type = ORDER_BUILD;
				orders->data[orders->count].target.building = j;
				orders->count += 1;
			}
		}

		// TODO don't add to orders if the order is too bad (negative rating) // TODO are there such orders?

		if (!region->train[0]) // no training in progress
		{
			for(j = 0; j < UNITS_COUNT; ++j)
			{
				double priority;

				if (!region_unit_available(region, UNITS[j])) continue;

				priority = troop_value(context, regions_info + i, troops_info, income, income_shortage, j);
				//LOG_DEBUG("%.*s | %.*s | %f", (int)region->name_length, region->name, (int)UNITS[j].name_length, UNITS[j].name, priority);
				if (priority < MAP_COMMAND_PRIORITY_THRESHOLD)
					continue;

				if (array_orders_expand(orders, orders->count + 1) < 0)
					goto error;
				orders->data[orders->count].priority = priority;
				orders->data[orders->count].region = region;
				orders->data[orders->count].type = ORDER_TRAIN;
				orders->data[orders->count].target.unit = j;
				orders->count += 1;
			}
		}
	}

	return 0;

error:
	array_orders_term(orders);
	return ERROR_MEMORY;
}

// Executes orders from the heap greedily until order priority becomes too low.
static void computer_map_orders_execute(struct heap_orders *restrict orders, const struct game *restrict game, unsigned char player, struct resources *restrict income)
{
	// Perform map orders until all actions are complete or until the priority becomes too low.
	// Skip orders for which there are not enough resources.

	while (orders->count)
	{
		struct region_order *order = orders->data[0];
		heap_orders_pop(orders);

		if (order->priority < MAP_COMMAND_PRIORITY_THRESHOLD)
			break;

		switch (order->type)
		{
		case ORDER_BUILD:
			if (order->region->construct >= 0)
				break;
			if (!resource_enough(&game->players[player].treasury, &BUILDINGS[order->target.building].cost))
				break;
			if (resources_adverse(income, &BUILDINGS[order->target.building].support))
				break;

			resource_add(&game->players[player].treasury, &BUILDINGS[order->target.building].cost);
			resource_add(income, &BUILDINGS[order->target.building].support); // TODO is this okay since the construction takes several turns?
			order->region->construct = order->target.building;
			//LOG_DEBUG("BUILD %.*s in %.*s | %f", (int)BUILDINGS[order->target.building].name_length, BUILDINGS[order->target.building].name, (int)order->region->name_length, order->region->name, order->priority);
			break;

		case ORDER_TRAIN:
			if (order->region->train[0])
				break;
			if (!resource_enough(&game->players[player].treasury, &UNITS[order->target.unit].cost))
				break;
			if (resources_adverse(income, &UNITS[order->target.unit].support))
				break;

			resource_add(&game->players[player].treasury, &UNITS[order->target.unit].cost);
			resource_add(income, &UNITS[order->target.unit].support);
			order->region->train[0] = UNITS + order->target.unit;
			//LOG_DEBUG("TRAIN %.*s in %.*s | %f", (int)UNITS[order->target.unit].name_length, UNITS[order->target.unit].name, (int)order->region->name_length, order->region->name, order->priority);
			break;
		}
	}
}

static unsigned map_state_neighbors(const struct game *restrict game, struct region *restrict region, const struct troop *restrict troop, struct region *restrict neighbors[static 1 + NEIGHBORS_LIMIT])
{
	unsigned neighbors_count = 0;

	const struct garrison_info *restrict garrison = garrison_info(region);
	if (garrison && (troop->move != LOCATION_GARRISON))
	{
		if (troop->owner == region->garrison.owner)
		{
			if (!region_garrison_full(region, garrison))
				neighbors[neighbors_count++] = LOCATION_GARRISON; // move to garrison
		}
		else if (!allies(game, troop->owner, region->garrison.owner))
		{
			neighbors[neighbors_count++] = LOCATION_GARRISON; // assault
		}
	}

	if (troop->move != region)
		neighbors[neighbors_count++] = region;

	// A troop can go to a neighboring region only if the owner of the troop also owns the current region.
	if (troop->owner == region->owner)
		for(size_t i = 0; i < NEIGHBORS_LIMIT; ++i)
			if (region->neighbors[i] && (troop->move != region->neighbors[i]))
				neighbors[neighbors_count++] = region->neighbors[i];

	return neighbors_count;
}

static void map_state_set(const struct game *restrict game, struct troop *restrict troop_moved, struct region *restrict move, struct region_info regions_info[static restrict REGIONS_LIMIT])
{
	size_t i;

	troop_moved->move = move;

	// Re-calculate the strength of the player in each region.
	for(i = 0; i < game->regions_count; ++i)
	{
		regions_info[i].strength.self = 0;
		regions_info[i].strength_garrison.self = 0;
	}
	for(i = 0; i < game->regions_count; ++i)
	{
		for(const struct troop *restrict troop = game->regions[i].troops; troop; troop = troop->_next)
		{
			double strength;

			if (troop->owner != troop_moved->owner)
				continue;

			strength = unit_importance(troop->unit, 0) * troop->count; // TODO is this okay?
			if (troop->move != LOCATION_GARRISON)
				regions_info[troop->move->index].strength.self += strength;
			else
				regions_info[i].strength_garrison.self += strength;
		}
	}
}

static double garrison_strength(const struct garrison_info *restrict info)
{
	// TODO use info to determine the strength
	return 2.0;
}

static inline double survivors_estimate(double strength, double strength_enemy)
{
	// TODO use a better formula to calculate survivors
	if (strength_enemy >= strength)
		return 0.0;
	else
		return 1.0 - (strength_enemy / strength) * (strength_enemy / strength);
}

static double rating_region(const struct game *restrict game, const struct region *restrict region, const struct region_info *restrict region_info, struct survivors *restrict survivors, unsigned char player)
{
	double rating;
	double strength, strength_enemy;

	strength = region_info->strength.self + region_info->strength_garrison.self + region_info->strength.ally * 0.5; // TODO is this a good estimate for allies?
	strength_enemy = region_info->strength_enemy + region_info->strength_enemy_neighbors * 0.5; // TODO is this a good estimate for neighboring enemies?

	if (region_info->neighbors_unknown)
		strength_enemy += 200.0; // TODO this number is completely arbitrary

	if (region_info->garrison && (region_info->strength.self || region_info->strength_garrison.self))
	{
		double strength_garrison, strength_garrison_enemy;

		// TODO the sum of the ratings for siege and assault may exceed rating_max for ownership
		if (!allies(game, region->garrison.owner, player))
		{
			// In case of an open battle, the assaulting army will participate. // TODO this is already added above
			//strength += region_info->strength_garrison.self;

			// rating for garrison siege
			if (!region_info->strength_garrison.self && region_info->strength.self)
			{
				rating += (double)region->garrison.siege / (region_info->garrison->provisions + 1);
//				printf("%10.*s garrison | %f\n", (int)region->name_length, region->name, (double)region->garrison.siege / (region_info->garrison->provisions + 1));
			}
		}

		// rating for garrison ownership
		strength_garrison = region_info->strength_garrison.self;
		strength_garrison_enemy = region_info->strength_enemy * garrison_strength(region_info->garrison);
		assert(strength_garrison >= 0);
		if (strength_garrison >= strength_garrison_enemy)
		{
			rating += 1.0;
//			printf("%10.*s garrison | %f\n", (int)region->name_length, region->name, 1.0);
		}

		// TODO use a better formula to calculate survivors
		if (strength_garrison && (strength_garrison >= strength_garrison_enemy))
			survivors->region_garrison = 1 - (strength_garrison_enemy / strength_garrison) * (strength_garrison_enemy / strength_garrison); // TODO this is a weird expression
	}

	assert(strength >= 0);
	if (strength >= strength_enemy)
	{
		// rating for region ownership
		rating += 1.0;
//		printf("%10.*s field | %f\n", (int)region->name_length, region->name, 1.0);
	}
	else if (strength > region_info->strength_enemy)
	{
		if (allies(game, region->owner, player))
		{
			// rating for region ownership
			rating += strength / strength_enemy;
//			printf("%10.*s field | %f\n", (int)region->name_length, region->name, strength / strength_enemy);
		}
	}

	if (strength)
		survivors->region = 0.5 * (survivors_estimate(strength, region_info->strength_enemy) + survivors_estimate(strength, strength_enemy));

	return rating;
}

static double map_state_rating(const struct game *restrict game, const struct array_troops *restrict troops, unsigned char player, const struct region_info regions_info[static restrict REGIONS_LIMIT], const struct troop_info *restrict troops_info, const unsigned char regions_visible[static restrict REGIONS_LIMIT], unsigned char regions_distances[static restrict REGIONS_LIMIT][REGIONS_LIMIT])
{
	double rating = 0.0, rating_max = 0.0;

	size_t i, j;

	// Consider as maximum rating the case when the player controls all regions and garrisons it can reach in one turn, no troops die and all needed troops are in the region they are needed.

	// TODO better handling for allies; maybe less rating for region/garrison owned by an ally
	// TODO do I rate assault properly

	struct survivors survivors[REGIONS_LIMIT] = {0};

	struct resources income = {0};

//printf("----\n");

	// Sum the ratings for each region.
	// Adjust rating_max for income.
	for(i = 0; i < game->regions_count; ++i)
	{
		struct resources income_region = {0};

		const struct region *restrict region = game->regions + i;

		if (!regions_info[i].nearby)
			continue;

		region_income(region, &income_region);
		resource_add(&income, &income_region);

		// Add rating proportional to the importance of the region.
		rating += rating_region(game, region, regions_info + i, survivors + i, player) * regions_info[i].importance;
		if (regions_info[i].garrison)
			rating_max += 2 * regions_info[i].importance;
		else
			rating_max += regions_info[i].importance;
	}
	rating_max += (income.food + income.wood + income.stone + income.gold + income.iron) * 200.0; // TODO this number is completely arbitrary

	// Adjust rating for income.
	income_calculate(game, &income, player);
	rating += (income.food + income.wood + income.stone + income.gold + income.iron) * 200.0; // TODO this number is completely arbitrary
//printf("income: %f\n", (income.food + income.wood + income.stone + income.gold + income.iron) * 200.0);

//	double s = 0, m = 0;

	// Sum the ratings for each troop.
	for(i = 0; i < troops->count; ++i)
	{
		struct region *region = troops->data[i].region;
		struct troop *troop = troops->data[i].troop;

		double troop_importance;

		unsigned class;
		unsigned troops_needed;

		// rating for surviving troops
		troop_importance = unit_importance(troop->unit, 0) * troop->count;
/*if ((troop->move == LOCATION_GARRISON) ? survivors[region->index].region_garrison : survivors[troop->move->index].region)
	printf("%10.*s %16.*s | %f\n", (int)region->name_length, region->name, (int)troop->unit->name_length, troop->unit->name, 0.5 * troop_importance * ((troop->move == LOCATION_GARRISON) ? survivors[region->index].region_garrison : survivors[troop->move->index].region));
s += 0.5 * troop_importance * ((troop->move == LOCATION_GARRISON) ? survivors[region->index].region_garrison : survivors[troop->move->index].region);*/
		rating += 0.5 * troop_importance * ((troop->move == LOCATION_GARRISON) ? survivors[region->index].region_garrison : survivors[troop->move->index].region);
		rating_max += 0.5 * troop_importance;

		class = unit_class(troop->unit);
		if (class & UNIT_VANGUARD)
			troops_needed = troops_info->vanguard;
		else if (class & UNIT_ASSAULT)
			troops_needed = troops_info->assault;
		else if (class & UNIT_RANGED)
			troops_needed = troops_info->ranged;
		else
		{
			assert(class & UNIT_FAST);
			troops_needed = troops_info->fast;
		}

		if (!troops_needed)
			continue;

		// rating for moving to a region where the troop is wanted
		if (troop->move != troop->location)
		{
			for(j = 0; j < game->regions_count; ++j)
			{
				unsigned troops_needed_region;

				if (class & UNIT_VANGUARD)
					troops_needed_region = regions_info[j].troops_needed.vanguard;
				else if (class & UNIT_ASSAULT)
					troops_needed_region = regions_info[j].troops_needed.assault;
				else if (class & UNIT_RANGED)
					troops_needed_region = regions_info[j].troops_needed.ranged;
				else
				{
					assert(class & UNIT_FAST);
					troops_needed_region = regions_info[j].troops_needed.fast;
				}

				rating += (regions_info[region->index].importance * troops_needed_region) / ((regions_distances[region->index][j] + 1) * troops_needed * 2.0); // TODO this 2.0 is completely arbitrary
/*m += (regions_info[region->index].importance * troops_needed_region) / ((regions_distances[region->index][j] + 1) * troops_needed * 2.0);
if (troops_needed_region)
	printf("%10.*s %16.*s # %f\n", (int)region->name_length, region->name, (int)troop->unit->name_length, troop->unit->name, (regions_info[region->index].importance * troops_needed_region) / ((regions_distances[region->index][j] + 1) * troops_needed * 2.0));*/
			}
		}
		rating_max += regions_info[region->index].importance / 2.0; // TODO this 2.0 is completely arbitrary
	}

//printf("s=%f m=%f rating=%f rating_max=%f\n", s, m, rating, rating_max);
	assert(rating_max);
	return rating / rating_max;
}

static int troops_find(const struct game *restrict game, struct array_troops *restrict troops, unsigned char player)
{
	for(size_t i = 0; i < game->regions_count; ++i)
	{
		struct troop *restrict troop;
		for(troop = game->regions[i].troops; troop; troop = troop->_next)
			if (troop->owner == player)
			{
				if (array_troops_expand(troops, troops->count + 1) < 0)
					return ERROR_MEMORY;
				troops->data[troops->count].region = game->regions + i;
				troops->data[troops->count].troop = troop;
				troops->count += 1;
			}
	}
	return 0;
}

// Choose suitable commands for player's troops using simulated annealing.
static int computer_map_move(const struct game *restrict game, unsigned char player, const unsigned char regions_visible[static restrict REGIONS_LIMIT], struct region_info *restrict regions_info, const struct troop_info *restrict troops_info, unsigned char regions_distances[static restrict REGIONS_LIMIT][REGIONS_LIMIT])
{
	double rating, rating_new;
	double temperature = 1.0;

	struct region *region;
	struct troop *troop;

	struct region *move_backup;

	struct region *neighbors[1 + NEIGHBORS_LIMIT];
	unsigned neighbors_count;

	size_t i, j;
	int status;

	// Find player troops.
	struct array_troops troops = {0};
	status = troops_find(game, &troops, player);
	if (status < 0) goto finally;

	if (!troops.count) goto finally; // nothing to do here if the player has no troops

	rating = map_state_rating(game, &troops, player, regions_info, troops_info, regions_visible, regions_distances);
	for(unsigned step = 0; step < ANNEALING_STEPS; ++step)
	{
		i = random() % troops.count;
		region = troops.data[i].region;
		troop = troops.data[i].troop;

		neighbors_count = map_state_neighbors(game, region, troop, neighbors);
		if (!neighbors_count) continue;

		// Remember current troop movement command and set a new one.
		move_backup = troop->move;
		map_state_set(game, troop, neighbors[random() % neighbors_count], regions_info);

		// Calculate the rating of the new set of commands.
		// Revert the new command if it is unacceptably worse than the current one.
		rating_new = map_state_rating(game, &troops, player, regions_info, troops_info, regions_visible, regions_distances);
		if (state_wanted(rating, rating_new, temperature)) rating = rating_new;
		else map_state_set(game, troop, move_backup, regions_info);

		temperature *= ANNEALING_COOLDOWN;
	}

	// Find the local maximum (best movement) for each of the troops.
	for(i = 0; i < troops.count; ++i)
	{
		region = troops.data[i].region;
		troop = troops.data[i].troop;

		neighbors_count = map_state_neighbors(game, region, troop, neighbors);
		for(j = 0; j < neighbors_count; ++j)
		{
			// Remember current troop movement command and set a new one.
			move_backup = troop->move;
			map_state_set(game, troop, neighbors[j], regions_info);

			// Calculate the rating of the new set of commands.
			// Revert the new command if it is worse than the current one.
			rating_new = map_state_rating(game, &troops, player, regions_info, troops_info, regions_visible, regions_distances);
			if (rating_new > rating) rating = rating_new;
			else map_state_set(game, troop, move_backup, regions_info);
		}
	}

	status = 0;

finally:
	array_troops_term(&troops);
	return status;
}

static double region_importance(const struct region *restrict region)
{
	// TODO estimate region importance (using income, etc.); currently it is designed to be good compared to unit_usefulness
	// self and ally regions have higher importance (defense is more important than attack)
	return 1000.0;
}

             struct region_info *regions_info_collect(const struct game *restrict game, unsigned char player, struct context *restrict context)
{
	size_t i, j;

	double importance;

	struct region_info *regions_info = malloc(game->regions_count * sizeof(struct region_info));
	if (!regions_info) return 0;

	// Calculate expected unit importance (used when the unit is unknown).
	context->count_expected = 0;
	for(i = 0; i < UNITS_COUNT; ++i)
	{
		context->count_expected += UNITS[i].troops_count;
		importance += unit_importance(UNITS + i, 0) * UNITS[i].troops_count;
	}
	context->count_expected = (context->count_expected + UNITS_COUNT / 2) / UNITS_COUNT;
	context->importance_expected = importance / (UNITS_COUNT * context->count_expected);

	// Determine which regions are visible to the player.
	map_visible(game, player, context->regions_visible);

	for(i = 0; i < game->regions_count; ++i)
	{
		const struct region *restrict region = game->regions + i;
		int enemy_visible;
		unsigned troops_hidden = 0;

		regions_info[i] = (struct region_info){.importance = region_importance(region)}; // TODO this overwrites strength

		regions_info[i].garrison = garrison_info(region);
		if (!allies(game, region->garrison.owner, player))
			regions_info[i].garrisons_enemy += 1;

		// Count neighboring regions that may pose danger.
		for(j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			const struct region *restrict neighbor = region->neighbors[j];
			if (!neighbor)
				continue;

			if (!allies(game, neighbor->garrison.owner, player))
				regions_info[i].garrisons_enemy += 1;

			// TODO what if the garrison is controlled by a different player
			if (!context->regions_visible[region->neighbors[j]->index])
				regions_info[i].neighbors_unknown += 1;
			else if (game->players[neighbor->owner].type == Neutral)
				regions_info[i].neighbors_neutral += 1;
			else if (!allies(game, region->neighbors[j]->owner, player))
				regions_info[i].neighbors_enemy += 1;
			regions_info[i].neighbors += 1;
		}

		if (context->regions_visible[i])
		{
			// TODO count enemy troops by class

			enemy_visible = (allies(game, region->owner, player) || allies(game, region->garrison.owner, player));

			// Determine the strength of the troops in the region according to their owner.
			for(const struct troop *restrict troop = region->troops; troop; troop = troop->_next)
			{
				// Take into account troops that are not visible.
				double strength = unit_importance(troop->unit, 0) * troop->count; // TODO is this okay?
				if (troop->owner == player)
				{
					// Collect information about the troop.
					unsigned class = unit_class(troop->unit);
					regions_info[i].troops.ranged += ((class & UNIT_RANGED) == UNIT_RANGED);
					regions_info[i].troops.assault += ((class & UNIT_ASSAULT) == UNIT_ASSAULT);
					regions_info[i].troops.fast += ((class & UNIT_FAST) == UNIT_FAST);
					regions_info[i].troops.vanguard += ((class & UNIT_VANGUARD) == UNIT_VANGUARD);
					regions_info[i].troops.total += 1;

					if (troop->location != LOCATION_GARRISON)
						regions_info[troop->location->index].strength.self += strength;
					else
						regions_info[i].strength_garrison.self += strength;
				}
				else if (allies(game, troop->owner, player))
				{
					if (troop->location != LOCATION_GARRISON)
						regions_info[i].strength.ally += strength;
					else
						regions_info[i].strength_garrison.ally += strength;
				}
				else if (enemy_visible)
				{
					if (troop->location != LOCATION_GARRISON)
						regions_info[i].strength_enemy += strength;
					else
						troops_hidden += troop->count;
				}
				else
				{
					if (troop->location != LOCATION_GARRISON)
						troops_hidden += troop->count;
					else
						troops_hidden += (unsigned)(context->count_expected * regions_info[i].garrison->troops / 2.0); // assume half the garrison is full
				}
			}
		}
		else troops_hidden = context->count_expected * TROOPS_EXPECTED;

		regions_info[i].strength_enemy += context->importance_expected * count_round(troops_hidden) * COUNT_ROUND_PRECISION;
	}

	// All internal region information is already collected.
	// Collect region information that depends on information about other regions.
	for(i = 0; i < game->regions_count; ++i)
	{
		const struct region *restrict region = game->regions + i;

		if (regions_info[i].strength.self || regions_info[i].strength_garrison.self || (region->owner == player))
			regions_info[i].nearby = true;

		for(j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			const struct region *restrict neighbor = region->neighbors[j];
			if (!neighbor)
				continue;

			if (regions_info[i].strength.self || (regions_info[i].strength_garrison.self && (region->owner == player)))
				regions_info[neighbor->index].nearby = true;

			// Don't treat neighboring neutral troops as enemies because they will not attack.
			if (game->players[region->owner].type != Neutral)
				regions_info[i].strength_enemy_neighbors += regions_info[neighbor->index].strength_enemy;
		}

		if (!regions_info[i].nearby)
			continue;
		region = game->regions + i;

		if (!regions_info[i].strength_enemy && !regions_info[i].strength_enemy_neighbors)
			continue; // no troops needed if the region is safe from enemies

		// Determine what troops each region needs.
		// TODO improve the logic here
		{
			double strength = regions_info[i].strength.self + regions_info[i].strength_garrison.self + (regions_info[i].strength.ally + regions_info[i].strength_garrison.ally) * 0.5; // TODO is this a good estimate for allies?
			double strength_enemies = regions_info[i].strength_enemy + regions_info[i].strength_enemy_neighbors;

			struct troop_info troop_info = regions_info[i].troops;
			bool assault = (regions_info[i].garrison && !allies(game, region->garrison.owner, player));

			while (strength < strength_enemies)
			{
				if (!troop_info.total || (troop_info.vanguard / troop_info.total < (assault ? 0.4 : 0.5)))
				{
					regions_info[i].troops_needed.vanguard += 1;
					troop_info.vanguard += 1;
				}
				else if (assault && (troop_info.assault / troop_info.total < 0.1))
				{
					regions_info[i].troops_needed.assault += 1;
					troop_info.assault += 1;
				}
				else if (troop_info.ranged / troop_info.total < 0.3)
				{
					regions_info[i].troops_needed.ranged += 1;
					troop_info.ranged += 1;
				}
				else if (troop_info.fast / troop_info.total < 0.2)
				{
					regions_info[i].troops_needed.fast += 1;
					troop_info.fast += 1;
				}
				else
				{
					regions_info[i].troops_needed.vanguard += 1;
					troop_info.vanguard += 1;
				}
				troop_info.total += 1;

				strength += context->importance_expected * context->count_expected;
			}
		}
	}

	return regions_info;
}

int computer_map(const struct game *restrict game, unsigned char player)
{
	struct resources income = {0};
	struct context context;
	struct region_info *restrict regions_info;
	struct troop_info troops_info = {0};
	struct array_orders orders;
	int status;

	unsigned char regions_distances[REGIONS_LIMIT][REGIONS_LIMIT] = {0}; // TODO currently 0 means unreachable; fix this
	assert(REGIONS_LIMIT <= 256); // TODO this should be a static assert

	// Compute the distance between each pair of regions with the Floyd-Warshall algorithm.
	distances_compute(game, regions_distances);

	// Collect information about each region.
	income_calculate(game, &income, player);
	regions_info = regions_info_collect(game, player, &context);
	if (!regions_info) return ERROR_MEMORY;

	// Count how many regions need a given troop.
	for(size_t i = 0; i < game->regions_count; ++i)
	{
		troops_info.ranged += regions_info[i].troops.ranged;
		troops_info.assault += regions_info[i].troops.assault;
		troops_info.fast += regions_info[i].troops.fast;
		troops_info.vanguard += regions_info[i].troops.vanguard;
		troops_info.total += regions_info[i].troops.total;
	}

	// Move player troops.
	status = computer_map_move(game, player, context.regions_visible, regions_info, &troops_info, regions_distances);

	// TODO support cancelling constructions and trainings
	status = computer_map_orders_list(&orders, game, player, &context, regions_info, &troops_info, &income);
	if (status < 0) goto finally;
	if (!orders.count) goto finally; // nothing to do

	// Create a priority queue with the available orders.
	struct heap_orders orders_queue;
	orders_queue.count = orders.count;
	orders_queue.data = malloc(orders.count * sizeof(*orders_queue.data));
	if (!orders_queue.data)
	{
		array_orders_term(&orders);
		status = ERROR_MEMORY;
		goto finally;
	}
	for(size_t i = 0; i < orders.count; ++i)
		orders_queue.data[i] = orders.data + i;
	heap_orders_heapify(&orders_queue);

	// Choose greedily which orders to execute.
	computer_map_orders_execute(&orders_queue, game, player, &income);

	free(orders_queue.data);
	array_orders_term(&orders);

finally:
	free(regions_info);
	return status;
}

int computer_invasion(const struct game *restrict game, const struct region *restrict region, unsigned char player)
{
	// TODO implement this
	return 1;
}

//////////////////////////////////

double tvalue(const struct game *restrict game, unsigned char player, struct region_info *restrict regions_info, const struct context *restrict context, const struct region *restrict region, size_t unit)
{
	struct resources income = {0};
	struct troop_info troops_info = {0};

	// Collect information about each region.
	income_calculate(game, &income, player);

	// Count how many regions need a given troop.
	for(size_t i = 0; i < game->regions_count; ++i)
	{
		troops_info.ranged += regions_info[i].troops.ranged;
		troops_info.assault += regions_info[i].troops.assault;
		troops_info.fast += regions_info[i].troops.fast;
		troops_info.vanguard += regions_info[i].troops.vanguard;
		troops_info.total += regions_info[i].troops.total;
	}

	bool income_shortage = ((income.gold < 0) || (income.food < 0) || (income.wood < 0) || (income.iron < 0) || (income.stone < 0));

	return troop_value(context, regions_info + (region - game->regions), &troops_info, &income, income_shortage, unit);
}

double bvalue(const struct game *restrict game, unsigned char player, struct region_info *restrict regions_info, const struct context *restrict context, const struct region *restrict region, size_t building)
{
	struct resources income = {0};
	struct troop_info troops_info = {0};

	// Collect information about each region.
	income_calculate(game, &income, player);

	// Count how many regions need a given troop.
	for(size_t i = 0; i < game->regions_count; ++i)
	{
		troops_info.ranged += regions_info[i].troops.ranged;
		troops_info.assault += regions_info[i].troops.assault;
		troops_info.fast += regions_info[i].troops.fast;
		troops_info.vanguard += regions_info[i].troops.vanguard;
		troops_info.total += regions_info[i].troops.total;
	}

	bool income_shortage = ((income.gold < 0) || (income.food < 0) || (income.wood < 0) || (income.iron < 0) || (income.stone < 0));

	return building_value(regions_info + (region - game->regions), &troops_info, &income, income_shortage, building);
}

/*double rate(const struct game *restrict game, unsigned char player, struct region_info *restrict regions_info, const struct context *restrict context)
{
	struct resources income = {0};
	struct troop_info troops_info = {0};
	int status;

	double rating = NAN;

	unsigned char regions_distances[REGIONS_LIMIT][REGIONS_LIMIT] = {0}; // TODO currently 0 means unreachable; fix this
	assert(REGIONS_LIMIT <= 256); // TODO this should be a static assert

	// Compute the distance between each pair of regions with the Floyd-Warshall algorithm.
	distances_compute(game, regions_distances);

	// Collect information about each region.
	income_calculate(game, &income, player);

	// Count how many regions need a given troop.
	for(size_t i = 0; i < game->regions_count; ++i)
	{
		troops_info.ranged += regions_info[i].troops.ranged;
		troops_info.assault += regions_info[i].troops.assault;
		troops_info.fast += regions_info[i].troops.fast;
		troops_info.vanguard += regions_info[i].troops.vanguard;
		troops_info.total += regions_info[i].troops.total;
	}

	// Find player troops.
	struct array_troops troops = {0};
	status = troops_find(game, &troops, player);
	if (status < 0)
		goto finally;

	if (!troops.count)
		goto finally; // nothing to do here if the player has no troops

	map_state_set(game, troops.data[0].troop, troops.data[0].troop->move, regions_info);
	rating = map_state_rating(game, &troops, player, regions_info, &troops_info, context->regions_visible, regions_distances);

	array_troops_term(&troops);

finally:
	return rating;
}*/
