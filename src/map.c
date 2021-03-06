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

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "draw.h"
#include "game.h"
#include "resources.h"
#include "map.h"

void troop_attach(struct troop **troops, struct troop *troop)
{
	troop->_prev = 0;
	troop->_next = *troops;
	if (*troops) (*troops)->_prev = troop;
	*troops = troop;
}

void troop_detach(struct troop **troops, struct troop *troop)
{
	if (troop->_prev) troop->_prev->_next = troop->_next;
	else *troops = troop->_next;
	if (troop->_next) troop->_next->_prev = troop->_prev;
}

void troop_remove(struct troop **troops, struct troop *troop)
{
	troop_detach(troops, troop);
	free(troop);
}

int troop_spawn(struct region *restrict region, struct troop **restrict troops, const struct unit *restrict unit, unsigned count, unsigned char owner)
{
	struct troop *troop = malloc(sizeof(*troop));
	if (!troop) return -1;

	troop->unit = unit;
	troop->count = count;
	troop->owner = owner;

	troop->dismiss = 0;

	troop->_prev = 0;
	troop->_next = *troops;
	if (*troops) (*troops)->_prev = troop;
	*troops = troop;
	troop->move = troop->location = region;

	return 0;
}

static inline unsigned population_income(unsigned workers, unsigned income)
{
	return (unsigned)(income * (workers / 1000.0));
}

void region_income(const struct region *restrict region, unsigned char player, struct resources *restrict income)
{
	for(const struct troop *restrict troop = region->troops; troop; troop = troop->_next)
	{
		const struct region *restrict destination;
		struct resources expense;

		if (troop->owner != player)
			continue;

		destination = ((troop->move == LOCATION_GARRISON) ? region : troop->move);
		if ((troop->owner == destination->owner) && (destination->owner == destination->garrison.owner))
		{
			if (troop->move == LOCATION_GARRISON)
				continue;

			// Troops expenses are covered by the move region.
			if (troop->move->owner != region->owner)
				resource_multiply(&expense, &troop->unit->support, 2 * troop->count);
			else
				resource_multiply(&expense, &troop->unit->support, troop->count);
			resource_add(income, &expense);
		}
		else
		{
			if ((troop->move == LOCATION_GARRISON) && (troop->owner == region->garrison.owner))
				continue; // sieged troop

			// Troop expenses are covered by another region. Double expenses.
			resource_multiply(&expense, &troop->unit->support, 2 * troop->count);
			resource_add(income, &expense);
		}

		if (region->owner == player)
		{
			income->gold -= 10 * sqrt(region->population / 1000.0); // region governing

			for(size_t i = 0; i < BUILDINGS_COUNT; ++i)
				if (region->built & (1 << i))
					resource_add(income, &BUILDINGS[i].support);
		}
	}
}

void region_production(const struct region *restrict region, struct resources *restrict result)
{
	struct resources income = {0};

	unsigned occupied = region->workers.food + region->workers.wood + region->workers.iron + region->workers.stone;

	unsigned workers_food = workers_population(region, region->workers.food);
	unsigned workers_wood = workers_population(region, region->workers.wood);
	unsigned workers_stone = workers_population(region, region->workers.stone);
	unsigned workers_iron = workers_population(region, region->workers.iron);

	for(size_t i = 0; i < BUILDINGS_COUNT; ++i)
		if (region->built & (1 << i))
			resource_add(&income, &BUILDINGS[i].income);

	result->food += population_income(workers_food, 20); // produced by workers
	result->food += population_income(workers_food, income.food); // produced by workers from buildings
	result->food -= population_income(region->population * (occupied / 100.0), 10); // consumed by workers

	//result->gold -= 10 * sqrt(region->population / 1000.0); // region governing
	result->gold -= population_income(workers_food + workers_wood + workers_stone + workers_iron, 20); // paid salaries
	result->gold += population_income(region->population, 10); // collected taxes
	// TODO gold mine support

	result->wood += population_income(((workers_wood > 2000) ? 2000 : workers_wood), income.wood); // produced by workers from buildings

	result->stone += population_income(((workers_stone > 2000) ? 2000 : workers_stone), income.stone); // produced by workers from buildings

	result->iron += population_income(((workers_iron > 2000) ? 2000 : workers_iron), income.iron); // produced by workers from buildings
}

// Chooses new region owner from the troops in the given alliance.
static unsigned region_owner_choose(const struct game *restrict game, struct region *restrict region, size_t troops_count, unsigned alliance)
{
	struct troop *troop;
	unsigned char owner_troop = random() % troops_count;

	for(troop = region->troops; troop; troop = troop->_next)
	{
		if (troop->move == LOCATION_GARRISON)
			continue;

		if (owner_troop) owner_troop -= 1;
		else return troop->owner;
	}

	// assert(0);
}

void region_battle_cleanup(const struct game *restrict game, struct region *restrict region, int assault, unsigned winner_alliance)
{
	struct troop *troop, *next;

	for(troop = region->troops; troop; troop = next)
	{
		next = troop->_next;

		// Remove dead troops.
		if (!troop->count)
		{
			troop_detach(&region->troops, troop);
			free(troop);
			continue;
		}

		// Set move destination of troops performing assault.
		if (assault && (troop->move == LOCATION_GARRISON))
			troop->move = troop->location;
	}
}

void region_turn_process(const struct game *restrict game, struct region *restrict region)
{
	// Region can change ownership if:
	// * it is conquered by enemy troops
	// * it is unguarded and the garrison's owner is an enemy of the region owner
	// Region garrison can change ownership if:
	// * it is assaulted successfully
	// * a siege finishes successfully
	// * it is unguarded, the region's owner is an enemy of the garrison owner and there are enemy troops in the region

	struct troop *troop, *next;

	bool region_guarded = false, region_garrison_guarded = false;
	unsigned invaders_count = 0;
	unsigned char invaders_alliance;

	// Collect information about the region.
	for(troop = region->troops; troop; troop = troop->_next)
	{
		if (troop->move == LOCATION_GARRISON) region_garrison_guarded = true;
		else if (troop->move == region) // make sure the troop is not retreating
		{
			region_guarded = true;
			if (!allies(game, troop->owner, region->owner))
			{
				invaders_count += 1;
				invaders_alliance = game->players[troop->owner].alliance;
			}
		}
	}

	// Liberated regions are re-conquered by the owner of the garrison.
	// Invaded regions are conquered by a random invading troop.
	if (invaders_count)
	{
		if (invaders_alliance == game->players[region->garrison.owner].alliance)
			region->owner = region->garrison.owner;
		else
			region->owner = region_owner_choose(game, region, invaders_count, invaders_alliance);
	}
	else if (!region_guarded && !allies(game, region->owner, region->garrison.owner))
		region->owner = region->garrison.owner;

	// Unguarded garrisons are conquered by enemy troops in the region.
	if (!region_garrison_guarded && region_guarded && !allies(game, region->owner, region->garrison.owner))
		region->garrison.owner = region->owner;

	// Handle siege events.
	if (allies(game, region->owner, region->garrison.owner))
	{
		region->garrison.siege = 0;
	}
	else
	{
		region->garrison.siege += 1;

		// If there are no more provisions in the garrison, kill the troops in it.
		const struct garrison_info *restrict garrison = garrison_info(region);
		// assert(garrison);
		if (region->garrison.siege > garrison->provisions) 
		{
			for(troop = region->troops; troop; troop = next)
			{
				next = troop->_next;
				if (troop->location == LOCATION_GARRISON)
				{
					troop_detach(&region->troops, troop);
					free(troop);
				}
			}

			// The siege ends. The garrison is conquered by the owner of the region.
			region->garrison.siege = 0;
			region->garrison.owner = region->owner;
		}
	}
}

// Returns whether the polygons share a border. Stores the border points in first and second (in the order of polygon a).
int polygons_border(const struct polygon *restrict a, const struct polygon *restrict b, struct point *restrict first, struct point *restrict second)
{
	size_t ai, bi;

	for(ai = 0; ai < a->vertices_count; ++ai)
	{
		for(bi = 0; bi < b->vertices_count; ++bi)
		{
			if (point_eq(a->points[ai], b->points[bi]))
			{
				size_t aj = (ai + 1) % a->vertices_count;
				size_t bj = (bi + b->vertices_count - 1) % b->vertices_count;
				if (point_eq(a->points[aj], b->points[bj]))
				{
					if (first) *first = a->points[ai];
					if (second) *second = a->points[aj];
					return 1;
				}
				else break; // a->points[ai] does not participate in a common border
			}
		}
	}

	return 0;
}

void region_orders_process(struct region *restrict region)
{
	// Update training time and check if there are trained units.
	if (region->train[0] && (++region->train_progress == region->train[0]->time))
	{
		if (troop_spawn(region, &region->troops, region->train[0], region->train[0]->troops_count, region->owner) < 0) abort(); // TODO

		region->train_progress = 0;
		for(size_t i = 1; i < TRAIN_QUEUE; ++i)
			region->train[i - 1] = region->train[i];
		region->train[TRAIN_QUEUE - 1] = 0;
	}

	// Update construction time and check if the building is finished.
	if ((region->construct >= 0) && (++region->build_progress == BUILDINGS[region->construct].time))
	{
		region->built |= (1 << region->construct);
		region->construct = -1;
		region->build_progress = 0;
	}
}

void region_orders_cancel(struct region *restrict region)
{
	region->construct = -1;
	region->build_progress = 0;
	for(size_t i = 0; i < TRAIN_QUEUE; ++i)
		region->train[i] = 0;
	region->train_progress = 0;
}

// Determine which regions are visible for the current player.
void map_visible(const struct game *restrict game, unsigned char player, unsigned char visible[REGIONS_LIMIT])
{
	memset(visible, 0, REGIONS_LIMIT);

	for(size_t i = 0; i < game->regions_count; ++i)
	{
		const struct region *restrict region = game->regions + i;

		if (allies(game, player, region->owner))
		{
			visible[i] = 1;

			// Make the neighboring regions visible when a watch tower is built.
			if (region_built(region, BuildingWatchTower))
			{
				for(size_t j = 0; j < NEIGHBORS_LIMIT; ++j)
				{
					struct region *neighbor = region->neighbors[j];
					if (neighbor) visible[neighbor->index] = 1;
				}
			}
		}
		else if (allies(game, player, region->garrison.owner)) visible[i] = 1;
	}
}

int region_garrison_full(const struct region *restrict region, const struct garrison_info *restrict garrison)
{
	unsigned count = 0;
	for(const struct troop *troop = region->troops; troop; troop = troop->_next)
		if ((troop->move == LOCATION_GARRISON) && (troop->owner == region->garrison.owner))
			count += 1;
	return (count == garrison->troops);
}

void region_troops_merge(struct region *restrict region)
{
	bool processed_players[PLAYERS_LIMIT] = {false}, processed_garrison = false;
	bool more;

	do
	{
		bool *processing = 0;
		struct troop *restrict troop_units[UNITS_COUNT] = {0};

		more = false;

		// Find troop that has not been processed and merge the troops from the same player.
		for(struct troop *restrict troop = region->troops; troop; troop = troop->_next)
		{
			bool *current = ((troop->location == LOCATION_GARRISON) ? &processed_garrison : &processed_players[troop->owner]);
			size_t index;

			if (!processing)
			{
				if (*current)
					continue;
				processing = current;
				*processing = true;
			}
			else if (current != processing)
			{
				if (!*current)
					more = true; // current troop is not processed yet
				continue;
			}

			index = troop->unit - UNITS;
			if (troop->count >= troop->unit->troops_count)
				continue;
			if (!troop_units[index]) troop_units[index] = troop;
			else
			{
				unsigned count_total = troop_units[index]->count + troop->count;
				if (count_total > troop->unit->troops_count)
				{
					// Cannot merge the troops. Transfer count to the current troop.
					troop->count = troop->unit->troops_count;
					troop_units[index]->count = count_total - troop->unit->troops_count;
				}
				else
				{
					troop->count = count_total;
					troop_detach(&region->troops, troop_units[index]);
					free(troop_units[index]);
					troop_units[index] = 0;
				}
			}
		}
	} while (more);
}
