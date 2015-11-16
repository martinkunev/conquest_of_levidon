#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "map.h"
#include "computer.h"
#include "computer_map.h"

#define MAP_COMMAND_PRIORITY_THRESHOLD 0.5 /* TODO maybe this should not be a macro */

#define RATING_DEFAULT 0.5

#define UNIT_IMPORTANCE_DEFAULT 10

#include <stdio.h>

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

struct region_info
{
	double importance;
	unsigned neighbors_enemy;
	unsigned neighbors_unknown;

	double strength_enemy;
	struct
	{
		double self, ally;
	} strength;
	struct
	{
		double self, ally;
	} strength_garrison;
};

static void income_calculate(const struct game *restrict game, struct resources *restrict result)
{
	size_t index;
	for(index = 0; index < game->regions_count; ++index)
	{
		const struct region *restrict region = game->regions + index;
		const struct troop *restrict troop;

		// Calculate region expenses.
		if (region->owner == region->garrison.owner)
		{
			// Troops expenses are covered by current region.
			for(troop = region->troops; troop; troop = troop->_next)
			{
				if (troop->move == LOCATION_GARRISON) continue;
				resource_add(result, &troop->unit->expense);
			}
		}
		else
		{
			// Troops expenses are covered by another region. Double expenses.
			for(troop = region->troops; troop; troop = troop->_next)
			{
				struct resources expense;
				if (troop->move == LOCATION_GARRISON) continue;
				resource_multiply(&expense, &troop->unit->expense, 2);
				resource_add(result, &expense);
			}
		}

		// Add region result if the garrison is not under siege.
		if (region->owner == region->garrison.owner)
		{
			struct resources income_region = {0};
			region_income(region, &income_region);
			resource_add(result, &income_region);
		}
	}
}

static int computer_map_orders_list(struct array_orders *restrict orders, const struct game *restrict game, unsigned char player, unsigned char regions_visible[static REGIONS_LIMIT], const struct region_info *restrict regions_info)
{
	size_t i, j;

	// TODO make sure the rating is between 0 and 1

	*orders = (struct array_orders){0};

	struct resources income;
	income_calculate(game, &income);

	// Make a list of the orders available for the player.
	for(i = 0; i < game->regions_count; ++i)
	{
		struct region *restrict region = game->regions + i;

		if ((region->owner != player) || (region->garrison.owner != player)) continue;

		unsigned neighbors_dangerous = regions_info[i].neighbors_unknown + regions_info[i].neighbors_enemy;

		// Don't check if there are enough resources as the first performed order will invalidate the check.
		// The check will be done before and if the order is executed.

		if (region->construct < 0) // no construction in progress
		{
			for(j = 0; j < buildings_count; ++j)
			{
				if (region_built(region, j)) continue;
				if (!region_building_available(region, buildings[j])) continue;

				if (array_orders_expand(orders, orders->count + 1) < 0)
					goto error;

				// TODO check what resources are lacking, etc.
				switch (j)
				{
				case BuildingWatchTower:
					orders->data[orders->count].priority = (regions_info[i].neighbors_unknown ? 1.0 : desire_buildings[j]);
					break;

				case BuildingFarm:
				case BuildingIrrigation:
				case BuildingSawmill:
				case BuildingMine:
				case BuildingBlastFurnace:
					orders->data[orders->count].priority = (neighbors_dangerous ? 0.5 * desire_buildings[j] : desire_buildings[j]);
					break;

				case BuildingBarracks:
				case BuildingArcheryRange:
				case BuildingStables:
				case BuildingWorkshop:
				case BuildingForge:
					orders->data[orders->count].priority = (neighbors_dangerous ? desire_buildings[j] : 0.5 * desire_buildings[j]);
					break;

				case BuildingPalisade:
				case BuildingFortress:
					orders->data[orders->count].priority = (neighbors_dangerous ? desire_buildings[j] : 0.5 * desire_buildings[j]);
					break;

				default:
					orders->data[orders->count].priority = desire_buildings[j] / (regions_info[i].neighbors_enemy + 1); // TODO this should be more complicated
					break;
				}
				orders->data[orders->count].region = region;
				orders->data[orders->count].type = ORDER_BUILD;
				orders->data[orders->count].target.building = j;
				orders->count += 1;
			}
		}

		if (!region->train[0]) // no training in progress
		{
			for(j = 0; j < UNITS_COUNT; ++j)
			{
				if (!region_unit_available(region, UNITS[j])) continue;

				if (array_orders_expand(orders, orders->count + 1) < 0)
					goto error;

				// TODO improve this
				double unit_value = unit_importance(UNITS + j) / (10.0 * unit_cost(UNITS + j)); // TODO this may be > 1
				if (!resource_enough(&income, &UNITS[j].expense)) unit_value /= 2;

				orders->data[orders->count].priority = (neighbors_dangerous ? unit_value : 0.5 * unit_value);
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

static void computer_map_orders_execute(struct heap_orders *restrict orders, const struct game *restrict game, unsigned char player)
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
			if (!resource_enough(&game->players[player].treasury, &buildings[order->target.building].cost))
				break;
			if (order->region->construct >= 0)
				break;
			resource_subtract(&game->players[player].treasury, &buildings[order->target.building].cost);
			order->region->construct = order->target.building;
			printf("build %.*s in %.*s | %f\n", (int)buildings[order->target.building].name_length, buildings[order->target.building].name, (int)order->region->name_length, order->region->name, order->priority);
			break;

		case ORDER_TRAIN:
			if (!resource_enough(&game->players[player].treasury, &UNITS[order->target.unit].cost))
				break;
			if (order->region->train[0])
				break;
			resource_subtract(&game->players[player].treasury, &UNITS[order->target.unit].cost);
			order->region->train[0] = UNITS + order->target.unit;
			printf("train %.*s in %.*s | %f\n", (int)UNITS[order->target.unit].name_length, UNITS[order->target.unit].name, (int)order->region->name_length, order->region->name, order->priority);
			break;
		}
	}
}

static unsigned map_state_neighbors(struct region *restrict region, const struct troop *restrict troop, struct region *restrict neighbors[static 1 + NEIGHBORS_LIMIT])
{
	unsigned neighbors_count = 0;

	size_t i;

	const struct garrison_info *restrict garrison = garrison_info(region);
	if (garrison && (troop->owner == region->garrison.owner) && (troop->move != LOCATION_GARRISON))
	{
		unsigned count = 0;

		// Count the troops in the garrison.
		const struct troop *troop_iterator;
		for(troop_iterator = region->troops; troop_iterator; troop_iterator = troop_iterator->_next)
			if ((troop_iterator->owner == troop->owner) && (troop_iterator->move == LOCATION_GARRISON))
				count += 1;

		if (count < garrison->troops) // if there is place for one more troop
			neighbors[neighbors_count++] = LOCATION_GARRISON;
	}

	if (troop->move != region)
		neighbors[neighbors_count++] = region;

	// A troop can go to a neighboring region only if the owner of the troop also owns the current region.
	if (troop->owner == region->owner)
		for(i = 0; i < NEIGHBORS_LIMIT; ++i)
			if (region->neighbors[i] && (troop->move != region->neighbors[i]))
				neighbors[neighbors_count++] = region;

	return neighbors_count;
}

static void map_state_set(struct troop *restrict troop, struct region *restrict region, struct region *restrict location, double strength, struct region_info regions_info[static restrict REGIONS_LIMIT])
{
	size_t index;

	index = ((troop->move != LOCATION_GARRISON) ? troop->move->index : region->index);
	regions_info[index].strength.self -= strength;

	troop->move = location;

	index = ((troop->move != LOCATION_GARRISON) ? troop->move->index : region->index);
	regions_info[index].strength.self += strength;
}

// Returns rating between 0 (worst) and 1 (best).
static double map_state_rating(const struct game *restrict game, unsigned char player, const struct region_info regions_info[static restrict REGIONS_LIMIT], const unsigned char regions_visible[static restrict REGIONS_LIMIT])
{
	double rating = 0.0, rating_max = 0.0;

	// TODO better handling for allies

	// TODO increase rating for troops in the garrison (due to reduced expenses)

	// Sum the ratings for each region.
	size_t i;
	for(i = 0; i < game->regions_count; ++i)
	{
		const struct region *restrict region = game->regions + i;

		double rating_region = 0.0;

		double strength = regions_info[i].strength.self + regions_info[i].strength.ally * 0.5;

		// Estimate the strength of neighboring regions.
		// TODO calculate enemy troops strength in neighboring regions
		double danger = regions_info[i].neighbors_enemy * 1000.0 + regions_info[i].neighbors_unknown * 600.0;

		rating_max += 1.0;
		if (allies(game, region->owner, player)) // the player defends a region
		{
			// Reduce the rating as much as the region is in danger.
			if (strength >= danger) rating_region += 1.0;
			else rating_region += strength / danger;

			if (!allies(game, region->owner, region->garrison.owner))
			{
				// The garrison is enemy and is sieged.
				// Adjust rating for assault.
				rating_max += 1.0;
				if (regions_info[i].strength_garrison.self)
				{
					const struct troop *restrict troop;
					const struct garrison_info *restrict garrison = garrison_info(region);

					// Estimate player strength for assault.
					// TODO take gate strength into account
					strength = 0;
					for(troop = region->troops; troop; troop = troop->_next)
					{
						if ((troop->owner != player) || (troop->move != LOCATION_GARRISON))
							continue;
						strength += unit_importance_assault(troop->unit, garrison) * troop->count;
					}

					if ((0.5 * strength) >= regions_info[i].strength_enemy) rating_region += 1.0;
					else rating_region += 0.5 * strength / regions_info[i].strength_enemy;
				}
				else rating_region += 0.5;
			}
		}
		else if (regions_info[i].strength.self) // the player attacks a region
		{
			if (!regions_visible[i]) continue;

			// Attacking is better than not attacking (> 0.5 rating) when the attacker is stronger than the defender.
			double strength_enemy = 250.0 + regions_info[i].strength_enemy + danger;

			// Set rating, accounting for the possibility of loss.
			if ((0.5 * strength) >= strength_enemy) rating_region += 1.0;
			else rating_region += 0.5 * strength / strength_enemy;

			if (!allies(game, region->owner, region->garrison.owner))
			{
				// The garrison is ally and is sieged.
				// Lifting the siege adds rating.
				rating_max += 1.0;
				if (strength > strength_enemy)
					rating_region += rating_region; // TODO multiply by region importance?
			}
		}
		else rating_region += 0.5;

		rating += rating_region * regions_info[i].importance;
	}

	// assert(rating_max);
	return rating / rating_max;
}

static int troops_find(const struct game *restrict game, struct array_troops *restrict troops, unsigned char player)
{
	size_t i;
	for(i = 0; i < game->regions_count; ++i)
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
	unsigned step;
	double rating, rating_new;
	double temperature = 1.0;

	struct region *region;
	struct troop *troop;

	double strength;
	struct region *move_backup;

	struct region *neighbors[1 + NEIGHBORS_LIMIT];
	unsigned neighbors_count;

	int status;

	size_t i, j;

	// Find player troops.
	struct array_troops troops = {0};
	status = troops_find(game, &troops, player);
	if (status < 0) goto finally;

	if (!troops.count) goto finally; // nothing to do here if the player has no troops

	rating = map_state_rating(game, player, regions_info, regions_visible);
	for(step = 0; step < ANNEALING_STEPS; ++step)
	{
		unsigned try;

		for(try = 0; try < ANNEALING_TRIES; ++try)
		{
			i = random() % troops.count;
			region = troops.data[i].region;
			troop = troops.data[i].troop;

			neighbors_count = map_state_neighbors(region, troop, neighbors);
			if (!neighbors_count) continue;

			// Remember current troop movement command and set a new one.
			strength = unit_importance(troop->unit) * troop->count;
			move_backup = troop->move;
			map_state_set(troop, region, neighbors[random() % neighbors_count], strength, regions_info);

			// Calculate the rating of the new set of commands.
			// Revert the new command if it is unacceptably worse than the current one.
			rating_new = map_state_rating(game, player, regions_info, regions_visible);
			if (state_wanted(rating, rating_new, temperature)) rating = rating_new;
			else map_state_set(troop, region, move_backup, strength, regions_info);
		}

		temperature *= 0.95;
	}

	// Find the local maximum (best movement) for each of the troops.
	for(i = 0; i < troops.count; ++i)
	{
		region = troops.data[i].region;
		troop = troops.data[i].troop;

		neighbors_count = map_state_neighbors(region, troop, neighbors);
		strength = unit_importance(troop->unit) * troop->count;
		for(j = 0; j < neighbors_count; ++j)
		{
			// Remember current troop movement command and set a new one.
			move_backup = troop->move;
			map_state_set(troop, region, neighbors[j], strength, regions_info);

			// Calculate the rating of the new set of commands.
			// Revert the new command if it is worse than the current one.
			rating_new = map_state_rating(game, player, regions_info, regions_visible);
			if (rating_new > rating) rating = rating_new;
			else map_state_set(troop, region, move_backup, strength, regions_info);
		}
	}

	status = 0;

finally:
	array_troops_term(&troops);
	return status;
}

static struct region_info *regions_info_collect(const struct game *restrict game, unsigned char player, unsigned char regions_visible[static REGIONS_LIMIT])
{
	const struct troop *restrict troop;
	double strength;

	size_t i, j;

	struct region_info *regions_info = malloc(game->regions_count * sizeof(struct region_info));
	if (!regions_info) return 0;

	// TODO is this useful? what to do if the garrison is controlled by a different player
	// Count the number of neighboring enemy regions.
	/*for(j = 0; j < NEIGHBORS_LIMIT; ++j)
	{
		const struct region *restrict neighbor = region->neighbors[j];

		if (!neighbor) continue;

		if (!regions_visible[neighbor->index])
			neighbors_unknown += 1;
		else if ((neighbor->owner != PLAYER_NEUTRAL) && !allies(game, player, neighbor->owner))
			neighbors_enemy += 1;
		else if ((neighbor->garrison.owner != PLAYER_NEUTRAL) && !allies(game, player, neighbor->garrison.owner))
			neighbors_enemy += 1;
	}*/

	for(i = 0; i < game->regions_count; ++i)
	{
		regions_info[i] = (struct region_info){.importance = 1.0}; // TODO estimate region importance

		if (!regions_visible[i]) continue;

		const struct region *restrict region = game->regions + i;
		int enemy_visible = (allies(game, region->owner, player) || allies(game, region->garrison.owner, player));

		// Count neighboring regions that may pose danger.
		for(j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			if (!region->neighbors[j]) continue;
			if (!regions_visible[region->neighbors[j]->index])
				regions_info[i].neighbors_unknown += 1;
			else if (!allies(game, region->owner, player))
				regions_info[i].neighbors_enemy += 1;
		}

		// Determine the strength of the troops in the region according to their owner.
		for(troop = region->troops; troop; troop = troop->_next)
		{
			// Take into account that enemy troops may not be visible.
			strength = unit_importance(troop->unit) * troop->count;
			if (troop->owner == player)
			{
				if (troop->location != LOCATION_GARRISON)
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
					regions_info[i].strength_enemy += UNIT_IMPORTANCE_DEFAULT * troop->count;
			}
			else if (troop->location != LOCATION_GARRISON)
			{
				regions_info[i].strength_enemy += UNIT_IMPORTANCE_DEFAULT * troop->count;
			}
		}
	}

	return regions_info;
}

int computer_map(const struct game *restrict game, unsigned char player)
{
	unsigned char regions_visible[REGIONS_LIMIT];

	struct array_orders orders;
	size_t i;

	int status;

	// TODO support cancelling buildings and trainings

	// Determine which regions are visible to the player.
	map_visible(game, player, regions_visible);

	// Collect information about each region.
	struct region_info *restrict regions_info = regions_info_collect(game, player, regions_visible);
	if (!regions_info) return ERROR_MEMORY;

	if (computer_map_orders_list(&orders, game, player, regions_visible, regions_info) < 0)
		goto error_memory;
	if (!orders.count)
	{
		free(regions_info);
		return 0;
	}

	// Create a priority queue with the available orders.
	struct heap_orders orders_queue;
	orders_queue.count = orders.count;
	orders_queue.data = malloc(orders.count * sizeof(*orders_queue.data));
	if (!orders_queue.data)
	{
		array_orders_term(&orders);
		goto error_memory;
	}
	for(i = 0; i < orders.count; ++i)
		orders_queue.data[i] = orders.data + i;
	heap_orders_heapify(&orders_queue);

	computer_map_orders_execute(&orders_queue, game, player);

	free(orders_queue.data);
	array_orders_term(&orders);

	// Move player troops.
	status = computer_map_move(game, player, regions_visible, regions_info);

	free(regions_info);
	return status;

error_memory:
	free(regions_info);
	return ERROR_MEMORY;
}
