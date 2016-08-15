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

#define SPEED_FAST 7

#define RANGED_PROPORTION (1.0 / 3.0)

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

enum {UNIT_RANGED = 0x1, UNIT_ASSAULT = 0x2, UNIT_FAST = 0x4};

struct region_info
{
	double importance;
	unsigned neighbors;
	unsigned neighbors_enemy;
	unsigned neighbors_unknown;

	double strength_enemy;
	double strength_enemy_neighbors;
	struct
	{
		double self, ally;
	} strength;
	struct
	{
		double self, ally;
	} strength_garrison; // garrison or assault strength

	const struct garrison_info *restrict garrison;
	unsigned garrisons_enemy;

	struct
	{
		unsigned ranged;
		unsigned assault;
		unsigned fast;
		unsigned total;
	} troops;

	bool nearby; // whether the player can reach the region in one turn
};

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
				resource_add(result, &troop->unit->income);
			}
			else
			{
				// Troop expenses are covered by another region. Double expenses.
				struct resources expense;
				resource_multiply(&expense, &troop->unit->income, 2);
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

	return class;
}

static double building_value(const struct region_info *restrict region_info, const struct resources *restrict income, bool income_shortage, size_t index)
{
	double value;

	unsigned neighbors_dangerous = region_info->neighbors_unknown + region_info->neighbors_enemy;

	switch (index)
	{
	case BuildingWatchTower:
		value = (region_info->neighbors_unknown ? ((double)region_info->neighbors_unknown / region_info->neighbors) : desire_buildings[index]);
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
		if (income_shortage && resource_enough(income, &BUILDINGS[index].income))
			value *= 2;

		// Check if the region is secure.
		if (!neighbors_dangerous)
			value *= 2;

		break;

	case BuildingPalisade:
	case BuildingFortress:
		value = region_info->troops.ranged * desire_buildings[index];
		value *= region_info->importance;

		// Check if the region is safe.
		if (!neighbors_dangerous)
			value *= 0.25;

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
			//value = unit_importance(units[index], 0) * units[index]->troops_count / expense_significance(&units[index]->cost);
			value = desire_buildings[index];

			if (class & UNIT_RANGED)
			{
				// Ranged troops are necessary for assault defenses and attack.
				if (region_info->garrison || (region_info->garrisons_enemy))
					value *= 1.5;
			}

			if (class & UNIT_ASSAULT)
			{
				// Assault troops are necessary if there are enemy garrisons nearby.
				if (region_info->garrisons_enemy)
					value *= 1.5;
			}

			// TODO fix the code below
			if (neighbors_dangerous)
				value *= 0.5;
			/*if (class & UNIT_FAST)
				;*/
		}

		break;
	}

	return value;
}

// TODO rewrite this
static double troop_value(const struct region_info *restrict region_info, const struct resources *restrict income, bool income_shortage, size_t index)
{
	double value;

	unsigned class;

	unsigned neighbors_dangerous = region_info->neighbors_unknown + region_info->neighbors_enemy;

	class = unit_class(UNITS + index);
	value = unit_importance(UNITS + index, 0) * UNITS[index].troops_count / expense_significance(&UNITS[index].cost);

	if (class & UNIT_RANGED)
	{
		// Ideally, a proportion of the troops must be ranged.
		if (region_info->troops.ranged)
			value *= (region_info->troops.total * RANGED_PROPORTION) / region_info->troops.ranged;
		else // TODO this is stupid
			value *= (region_info->troops.total * RANGED_PROPORTION);
	}

	if (class & UNIT_ASSAULT)
	{
		// The value of training an assault machine is larger if there are not enough assault machines.
		if (region_info->troops.assault < region_info->garrisons_enemy)
			value *= (region_info->troops.assault ? 2 : 3);
	}

	if (class & UNIT_FAST)
	{
		// TODO add something here
	}

	if (class & (UNIT_RANGED | UNIT_ASSAULT))
	{
		// Supporting troops are necessary to protect ranged troops.
		if (region_info->troops.ranged)
			value *= region_info->troops.ranged / (region_info->troops.total * RANGED_PROPORTION);
	}

	if (!neighbors_dangerous)
		value *= 0.5;

	return value;
}

static int computer_map_orders_list(struct array_orders *restrict orders, const struct game *restrict game, unsigned char player, unsigned char regions_visible[static REGIONS_LIMIT], const struct region_info *restrict regions_info, const struct resources *restrict income)
{
	size_t i, j;

	bool income_shortage = ((income->gold < 0) || (income->food < 0) || (income->wood < 0) || (income->iron < 0) || (income->stone < 0));

	// Make a list of the orders available for the player.
	*orders = (struct array_orders){0};

	// TODO make sure the rating is between 0 and 1 // TODO why?

	printf(">>> start <<<\n");

	for(i = 0; i < game->regions_count; ++i)
	{
		struct region *restrict region = game->regions + i;
		unsigned neighbors_dangerous;

		if ((region->owner != player) || (region->garrison.owner != player))
			continue;

		neighbors_dangerous = regions_info[i].neighbors_unknown + regions_info[i].neighbors_enemy;

		// Don't check if there are enough resources and enough income, as the first performed order will invalidate the checks.
		// The checks will be done before and if the order is executed.

		if (region->construct < 0) // no construction in progress
		{
			for(j = 0; j < BUILDINGS_COUNT; ++j)
			{
				double priority;

				if (region_built(region, j) || !region_building_available(region, BUILDINGS[j]))
					continue;

				priority = building_value(regions_info + i, income, income_shortage, j);
				LOG_DEBUG("%.*s | %.*s | %f", (int)region->name_length, region->name, (int)BUILDINGS[j].name_length, BUILDINGS[j].name, priority);
				if (priority < 0) // TODO is this necessary?
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

				priority = troop_value(regions_info + i, income, income_shortage, j);
				//LOG_DEBUG("%.*s | %.*s | %f", (int)region->name_length, region->name, (int)UNITS[j].name_length, UNITS[j].name, priority);
				if (priority < 0) // TODO is this necessary?
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
static void computer_map_orders_execute(struct heap_orders *restrict orders, const struct game *restrict game, unsigned char player, const struct resources *restrict income)
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
			if (!resource_enough(income, &BUILDINGS[order->target.building].income))
				break;

			resource_add(&game->players[player].treasury, &BUILDINGS[order->target.building].cost);
			order->region->construct = order->target.building;
			LOG_DEBUG("BUILD %.*s in %.*s | %f", (int)BUILDINGS[order->target.building].name_length, BUILDINGS[order->target.building].name, (int)order->region->name_length, order->region->name, order->priority);
			break;

		case ORDER_TRAIN:
			if (order->region->train[0])
				break;
			if (!resource_enough(&game->players[player].treasury, &UNITS[order->target.unit].cost))
				break;
			if (!resource_enough(income, &UNITS[order->target.unit].income))
				break;

			resource_add(&game->players[player].treasury, &UNITS[order->target.unit].cost);
			order->region->train[0] = UNITS + order->target.unit;
			//LOG_DEBUG("TRAIN %.*s in %.*s | %f", (int)UNITS[order->target.unit].name_length, UNITS[order->target.unit].name, (int)order->region->name_length, order->region->name, order->priority);
			break;
		}
	}
}

static unsigned map_state_neighbors(struct region *restrict region, const struct troop *restrict troop, struct region *restrict neighbors[static 1 + NEIGHBORS_LIMIT])
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
		else neighbors[neighbors_count++] = LOCATION_GARRISON; // assault
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
			if (troop->location != LOCATION_GARRISON) // TODO should I use troop->move instead
				regions_info[i].strength.self += strength;
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

static double map_state_rating(const struct game *restrict game, unsigned char player, const struct region_info regions_info[static restrict REGIONS_LIMIT], const unsigned char regions_visible[static restrict REGIONS_LIMIT])
{
	double rating = 0.0, rating_max = 0.0;

	// Consider as maximum rating the case when the player controls all regions and garrisons it can reach in one turn and no troops die.

	// TODO better handling for allies; maybe less rating for region/garrison owned by an ally
	// TODO do I rate assault properly

	// TODO use a better formula to calculate survivors
	struct
	{
		double region;
		double region_garrison;
	} survivors[REGIONS_LIMIT] = {0};

	struct resources income;

	// Sum the ratings for each region.
	// Adjust rating_max for income.
	for(size_t i = 0; i < game->regions_count; ++i)
	{
		double rating_region = 0.0, rating_max_region = 0.0;
		const struct region *restrict region;

		if (!regions_visible[i])
			continue;
		region = game->regions + i;

		// TODO take into account that neutral player cannot attack (game->players[...].type == Neutral)

		if (regions_info[i].nearby)
		{
			double strength = regions_info[i].strength.self + regions_info[i].strength.ally * 0.5; // TODO is this a good estimate for allies?
			double strength_enemy = regions_info[i].strength_enemy + regions_info[i].strength_enemy_neighbors;

			struct resources income_region = {0};

			if (regions_info[i].neighbors_unknown)
				strength_enemy += 200.0; // TODO this number is completely arbitrary

			if (regions_info[i].garrison && (regions_info[i].strength.self || regions_info[i].strength_garrison.self))
			{
				double strength_garrison, strength_garrison_enemy;

				if (!allies(game, region->garrison.owner, player))
				{
					// In case of an open battle, the assaulting army will participate.
					strength += regions_info[i].strength_garrison.self;

					// rating for garrison siege
					if (!regions_info[i].strength_garrison.self && regions_info[i].strength.self)
						rating_region += region->garrison.siege / (regions_info[i].garrison->provisions + 1);
				}

				// rating for garrison ownership
				strength_garrison = regions_info[i].strength_garrison.self;
				strength_garrison_enemy = regions_info[i].strength_enemy * garrison_strength(regions_info[i].garrison);
				assert(strength_garrison >= 0);
				rating_region += (strength_garrison > strength_garrison_enemy);
				rating_max_region += 1.0;

				if (strength_garrison && (strength_garrison >= strength_garrison_enemy))
					survivors[i].region_garrison = 1 - strength_garrison_enemy / strength_garrison;
			}

			// rating for region ownership
			assert(strength >= 0);
			rating_region += (strength > strength_enemy);
			rating_max_region += 1.0;

			if (strength && (strength > strength_enemy))
				survivors[i].region = 1.0 - strength_enemy / strength;

			region_income(region, &income_region);
			resource_add(&income, &income_region);
		}

		// Add rating proportional to the importance of the region.
		rating += rating_region * regions_info[i].importance;
		rating_max += rating_max_region * regions_info[i].importance;
	}
	rating_max += (income.food + income.wood + income.stone + income.gold + income.iron) * 200.0; // TODO this number is completely arbitrary

	// Adjust rating for income.
	income_calculate(game, &income, player);
	rating += (income.food + income.wood + income.stone + income.gold + income.iron) * 200.0; // TODO this number is completely arbitrary

	// Sum the ratings for each troop.
	for(size_t i = 0; i < game->regions_count; ++i)
	{
		if (!regions_visible[i])
			continue;

		for(const struct troop *restrict troop = game->regions[i].troops; troop; troop = troop->_next)
		{
			double troop_importance;

			if (troop->owner != player)
				continue;

			// rating for surviving troops
			troop_importance = unit_importance(troop->unit, 0) * troop->count;
			rating += troop_importance * ((troop->move == LOCATION_GARRISON) ? survivors[i].region_garrison : survivors[troop->move->index].region);
			rating_max += troop_importance;
		}
	}

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
static int computer_map_move(const struct game *restrict game, unsigned char player, const unsigned char regions_visible[static restrict REGIONS_LIMIT], struct region_info *restrict regions_info)
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

	rating = map_state_rating(game, player, regions_info, regions_visible);
	for(unsigned step = 0; step < ANNEALING_STEPS; ++step)
	{
		for(unsigned try = 0; try < ANNEALING_TRIES; ++try)
		{
			i = random() % troops.count;
			region = troops.data[i].region;
			troop = troops.data[i].troop;

			neighbors_count = map_state_neighbors(region, troop, neighbors);
			if (!neighbors_count) continue;

			// Remember current troop movement command and set a new one.
			move_backup = troop->move;
			map_state_set(game, troop, neighbors[random() % neighbors_count], regions_info);

			// Calculate the rating of the new set of commands.
			// Revert the new command if it is unacceptably worse than the current one.
			rating_new = map_state_rating(game, player, regions_info, regions_visible);
			if (state_wanted(rating, rating_new, temperature)) rating = rating_new;
			else map_state_set(game, troop, move_backup, regions_info);
		}

		temperature *= ANNEALING_COOLDOWN;
	}

	// Find the local maximum (best movement) for each of the troops.
	for(i = 0; i < troops.count; ++i)
	{
		region = troops.data[i].region;
		troop = troops.data[i].troop;

		neighbors_count = map_state_neighbors(region, troop, neighbors);
		for(j = 0; j < neighbors_count; ++j)
		{
			// Remember current troop movement command and set a new one.
			move_backup = troop->move;
			map_state_set(game, troop, neighbors[j], regions_info);

			// Calculate the rating of the new set of commands.
			// Revert the new command if it is worse than the current one.
			rating_new = map_state_rating(game, player, regions_info, regions_visible);
			if (rating_new > rating) rating = rating_new;
			else map_state_set(game, troop, move_backup, regions_info);
		}
	}

	status = 0;

finally:
	array_troops_term(&troops);

	return status;
}

static struct region_info *regions_info_collect(const struct game *restrict game, unsigned char player, unsigned char regions_visible[static REGIONS_LIMIT])
{
	size_t i;

	double importance_expected = 0;

	struct region_info *regions_info = malloc(game->regions_count * sizeof(struct region_info));
	if (!regions_info) return 0;

	// Calculate expected unit importance (used when the unit is unknown).
	for(i = 0; i < UNITS_COUNT; ++i)
		importance_expected += unit_importance(UNITS + i, 0);
	importance_expected /= UNITS_COUNT;

	for(i = 0; i < game->regions_count; ++i)
	{
		const struct region *restrict region = game->regions + i;
		int enemy_visible;
		unsigned troops_hidden = 0;

		regions_info[i] = (struct region_info){.importance = 1000.0}; // TODO estimate region importance (using income, etc.); currently it is designed to be good compared to unit_usefulness

		if (!regions_visible[i]) continue;

		enemy_visible = (allies(game, region->owner, player) || allies(game, region->garrison.owner, player));

		regions_info[i].garrison = garrison_info(region);
		if (!allies(game, region->garrison.owner, player))
			regions_info[i].garrisons_enemy += 1;

		// Count neighboring regions that may pose danger.
		for(size_t j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			const struct region *restrict neighbor = region->neighbors[j];
			if (!neighbor)
				continue;

			if (!allies(game, neighbor->garrison.owner, player))
				regions_info[i].garrisons_enemy += 1;

			// TODO what if the garrison is controlled by a different player
			if (!regions_visible[region->neighbors[j]->index])
				regions_info[i].neighbors_unknown += 1;
			else if (!allies(game, region->neighbors[j]->owner, player))
				regions_info[i].neighbors_enemy += 1;
			regions_info[i].neighbors += 1;
		}

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
				regions_info[i].troops.total += 1;

				if (troop->location != LOCATION_GARRISON) // TODO should I use troop->move instead
					regions_info[i].strength.self += strength;
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
			else if (troop->location != LOCATION_GARRISON)
			{
				troops_hidden += troop->count;
			}
		}

		regions_info[i].strength_enemy += importance_expected * count_round(troops_hidden) * COUNT_ROUND_PRECISION;
	}

	// All internal region information is already collected.
	// Collect region information that depends on information about other regions.
	for(i = 0; i < game->regions_count; ++i)
	{
		const struct region *restrict region = game->regions + i;

		if (regions_info[i].strength.self || regions_info[i].strength_garrison.self || (region->owner == player))
			regions_info[i].nearby = true;

		for(size_t j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			const struct region *restrict neighbor = region->neighbors[j];
			if (!neighbor)
				continue;

			if (regions_info[i].strength.self || (regions_info[i].strength_garrison.self && (region->owner == player)))
				regions_info[neighbor->index].nearby = true;

			regions_info[i].strength_enemy_neighbors += regions_info[neighbor->index].strength_enemy;
		}
	}

	return regions_info;
}

int computer_map(const struct game *restrict game, unsigned char player)
{
	struct resources income = {0};
	unsigned char regions_visible[REGIONS_LIMIT];
	struct region_info *restrict regions_info;
	struct array_orders orders;
	int status;

	// TODO support cancelling buildings and trainings

	income_calculate(game, &income, player);

	// Determine which regions are visible to the player.
	map_visible(game, player, regions_visible);

	// Collect information about each region.
	regions_info = regions_info_collect(game, player, regions_visible);
	if (!regions_info) return ERROR_MEMORY;

	status = computer_map_orders_list(&orders, game, player, regions_visible, regions_info, &income);
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

	// Move player troops.
	status = computer_map_move(game, player, regions_visible, regions_info);

finally:
	free(regions_info);
	return status;
}
