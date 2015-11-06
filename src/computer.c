#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "combat.h"
#include "movement.h"
#include "computer.h"

#define FLOAT_PRECISION 0.001

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

struct pawn_command
{
	enum pawn_action action;
	struct point target;

	size_t moves_count;
	struct move moves[];
};

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

/*struct combat_victims
{
	const struct pawn *central;
	const struct pawn *lateral[4];
	const struct pawn *diagonal[4];
	size_t central_count;
	size_t lateral_count;
	size_t diagonal_count;
};*/

//enum {SEARCH_TRIES = 1024};
enum {SEARCH_TRIES = 64};

enum {ANNEALING_STEPS = 128, ANNEALING_TRIES = 16};

static const double desire_buildings[] =
{
	[BuildingFarm] = 1.0,
	[BuildingIrrigation] = 0.5,
	[BuildingSawmill] = 1.1,
	[BuildingMine] = 0.7,
	[BuildingBlastFurnace] = 0.5,
	[BuildingBarracks] = 0.8,
	[BuildingArcheryRange] = 0.8,
	[BuildingStables] = 0.4,
	[BuildingWatchTower] = 1.0,
	[BuildingPalisade] = 0.6,
	[BuildingFortress] = 0.4,
	[BuildingWorkshop] = 0.3,
	[BuildingForge] = 0.5,
};

static const double desire_units[] =
{
	[UnitPeasant] = 0.3,
	[UnitMilitia] = 0.5,
	[UnitPikeman] = 0.6,
	[UnitArcher] = 0.5,
	[UnitLongbow] = 0.6,
	[UnitLightCavalry] = 0.7,
	[UnitBatteringRam] = 0.4,
};

// TODO remove the first argument (it is not valid outside of battle)
static double unit_importance(const struct battle *restrict battle, const struct unit *restrict unit)
{
	// unit				importance	count
	// Peasant:			7			25
	// Militia:			9			25
	// Pikeman:			10			25
	// Archer:			8.5			25
	// Longbow:			13			25
	// Light cavalry:	14			16
	// Battering ram:	72.5		1

	// TODO more sophisticated logic here
	// TODO importance should depend on battle obstacles (e.g. the more they are, the more important is battering ram)

	return unit->health + unit->melee.damage * unit->melee.agility * 2 + unit->ranged.damage * 3;
}

static int computer_map_orders_list(struct array_orders *restrict orders, const struct game *restrict game, unsigned char player, unsigned char regions_visible[REGIONS_LIMIT])
{
	size_t i, j;

	*orders = (struct array_orders){0};

	// Make a list of the orders available for the player.
	for(i = 0; i < game->regions_count; ++i)
	{
		struct region *restrict region = game->regions + i;

		unsigned neighbors_unknown = 0, neighbors_enemy = 0;

		if ((region->owner != player) || (region->garrison.owner != player)) continue;

		// Count the number of neighboring enemy regions.
		for(j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			const struct region *restrict neighbor = region->neighbors[j];

			if (!neighbor) continue;

			if (!regions_visible[neighbor->index])
				neighbors_unknown += 1;
			else if ((neighbor->owner != PLAYER_NEUTRAL) && !allies(game, player, neighbor->owner))
				neighbors_enemy += 1;
			else if ((neighbor->garrison.owner != PLAYER_NEUTRAL) && !allies(game, player, neighbor->garrison.owner))
				neighbors_enemy += 1;
		}

		if (region->construct < 0) // no construction in progress
		{
			for(j = 0; j < buildings_count; ++j)
			{
				if (region_built(region, j)) continue;
				if (!region_building_available(region, buildings[j])) continue;

				// Don't check if there are enough resources as the first performed order will invalidate the check.

				// TODO if neighbors_unknown, increase the priority of watch tower

				if (array_orders_expand(orders, orders->count + 1) < 0)
					goto error;

				switch (j)
				{
				case BuildingWatchTower:
					orders->data[orders->count].priority = desire_buildings[j] * neighbors_unknown; // TODO this should be more complicated
					break;

				default:
					orders->data[orders->count].priority = desire_buildings[j] / (neighbors_enemy + 1); // TODO this should be more complicated
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

				// Don't check if there are enough resources as the first performed order will invalidate the check.

				if (array_orders_expand(orders, orders->count + 1) < 0)
					goto error;

				orders->data[orders->count].priority = desire_units[j] * (neighbors_enemy + neighbors_unknown / 2); // TODO this should be more complicated
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

static int state_wanted(double rate, double rate_new, double temperature)
{
	// e ^ ((rate_new - rate) / temperature)

	double probability = exp((rate_new - rate) / temperature);
	return probability > ((double)random() / RAND_MAX);
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
			if ((region->neighbors[i]) && (troop->move != region->neighbors[i]))
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
			// assert(!regions_info[i].strength_enemy);

			// Reduce the rating as much as the region is in danger.
			if (strength >= danger) rating_region += 1.0;
			else rating_region += strength / danger;
		}
		else if (regions_info[i].strength.self) // the player attacks a region
		{
			if (!regions_visible[i]) continue;

			// Reduce the rating to account for the possible loss.
			// Attacking is better than not attacking (> 0.5 rating) when the attacker is stronger than the defender.
			double strength_enemy = 250.0 + regions_info[i].strength_enemy + danger;
			if ((0.5 * strength) >= strength_enemy) rating_region += 1.0;
			else rating_region += 0.5 * strength / strength_enemy;
		}
		else
		{
			rating_region += 0.5;
		}

		if (garrison_info(region))
		{
			// regions_info[i].strength_garrison.self

			// TODO adjust for garrison
		}

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
static int computer_map_move(const struct game *restrict game, unsigned char player, const unsigned char regions_visible[static restrict REGIONS_LIMIT])
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

	// Collect information about each region.
	struct region_info *regions_info = malloc(game->regions_count * sizeof(struct region_info));
	if (!regions_info) return ERROR_MEMORY;
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
			strength = unit_importance(0, troop->unit) * troop->count;
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

	// Find player troops.
	struct array_troops troops = {0};
	status = troops_find(game, &troops, player);
	if (status < 0) goto finally;

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
			// TODO is it okay to just choose one neighbor at random?
			strength = unit_importance(0, troop->unit) * troop->count;
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
		strength = unit_importance(0, troop->unit) * troop->count;
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
	free(regions_info);
	return status;
}

int computer_map(const struct game *restrict game, unsigned char player)
{
	unsigned char regions_visible[REGIONS_LIMIT];

	struct array_orders orders;
	size_t i;

	// Determine which regions are visible to the player.
	map_visible(game, player, regions_visible);

	if (computer_map_orders_list(&orders, game, player, regions_visible) < 0)
		return ERROR_MEMORY;
	if (!orders.count)
		return 0;

	// Create a priority queue with the available orders.
	struct heap_orders orders_queue;
	orders_queue.count = orders.count;
	orders_queue.data = malloc(orders.count * sizeof(*orders_queue.data));
	if (!orders_queue.data)
	{
		array_orders_term(&orders);
		return ERROR_MEMORY;
	}
	for(i = 0; i < orders.count; ++i)
		orders_queue.data[i] = orders.data + i;
	heap_orders_heapify(&orders_queue);

	computer_map_orders_execute(&orders_queue, game, player);

	free(orders_queue.data);
	array_orders_term(&orders);

	// TODO cancel buildings and trainings

	return computer_map_move(game, player, regions_visible);
}

static void battle_state_change(const struct game *restrict game, const struct battle *restrict battle, struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	unsigned speed = pawn->troop->unit->speed;
	struct point location = pawn->moves[pawn->moves_count - 1].location, target;

	struct point neighbors[4];
	unsigned neighbors_count = 0;

	target = (struct point){location.x - 1, location.y};
	if ((target.x >= 0) && (speed >= reachable[target.y][target.x]) && battlefield_passable(game, &battle->field[target.y][target.x], pawn->troop->owner))
		neighbors[neighbors_count++] = target;

	target = (struct point){location.x + 1, location.y};
	if ((target.x < BATTLEFIELD_WIDTH) && (speed >= reachable[target.y][target.x]) && battlefield_passable(game, &battle->field[target.y][target.x], pawn->troop->owner))
		neighbors[neighbors_count++] = target;

	target = (struct point){location.x, location.y - 1};
	if ((target.y >= 0) && (speed >= reachable[target.y][target.x]) && battlefield_passable(game, &battle->field[target.y][target.x], pawn->troop->owner))
		neighbors[neighbors_count++] = target;

	target = (struct point){location.x, location.y + 1};
	if ((target.y < BATTLEFIELD_HEIGHT) && (speed >= reachable[target.y][target.x]) && battlefield_passable(game, &battle->field[target.y][target.x], pawn->troop->owner))
		neighbors[neighbors_count++] = target;

	// TODO support assault

	if (neighbors_count)
	{
		struct point neighbor = neighbors[random() % neighbors_count];
		struct pawn *restrict target_pawn = battle->field[neighbor.y][neighbor.x].pawn;

		if (target_pawn)
		{
			if (combat_order_shoot(game, battle, obstacles, pawn, neighbor))
				return;
			else if (combat_order_fight(game, battle, obstacles, pawn, target_pawn))
				return;
		}

		pawn->moves_count = 1;
		movement_queue(pawn, neighbor, graph, obstacles);
	}
}

static unsigned victims_fight_find(const struct game *restrict game, const struct pawn *restrict pawn, unsigned char player, const struct pawn *pawns[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], const struct pawn *restrict victims[static 4])
{
	const struct pawn *victim;
	unsigned victims_count = 0;

	struct point location, target;

	// TODO what if one of the pawns is on a tower

	if (pawn->troop->owner == player)
	{
		location = pawn->moves[pawn->moves_count - 1].location; // assume pawn movement will be completed by the end of the round

		// If the pawn has a specific fight target and is able to fight it, fight only that target.
		if ((pawn->action == PAWN_FIGHT) && battlefield_neighbors(location, pawn->target.pawn->moves[0].location))
		{
			victims[victims_count++] = pawn->target.pawn;
			return victims_count;
		}
	}
	else location = pawn->moves[0].location;

	// Look for pawns to fight at the neighboring fields.

	target = (struct point){location.x - 1, location.y};
	if ((target.x >= 0) && (victim = pawns[target.y][target.x]) && !allies(game, pawn->troop->owner, victim->troop->owner))
		victims[victims_count++] = victim;

	target = (struct point){location.x + 1, location.y};
	if ((target.x < BATTLEFIELD_WIDTH) && (victim = pawns[target.y][target.x]) && !allies(game, pawn->troop->owner, victim->troop->owner))
		victims[victims_count++] = victim;

	target = (struct point){location.x, location.y - 1};
	if ((target.y >= 0) && (victim = pawns[target.y][target.x]) && !allies(game, pawn->troop->owner, victim->troop->owner))
		victims[victims_count++] = victim;

	target = (struct point){location.x, location.y + 1};
	if ((target.y < BATTLEFIELD_HEIGHT) && (victim = pawns[target.y][target.x]) && !allies(game, pawn->troop->owner, victim->troop->owner))
		victims[victims_count++] = victim;

	return victims_count;
}

/*static void victims_shoot_find(const struct pawn *restrict pawn, const struct pawn *pawns[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct combat_victims *restrict victims)
{
	const struct pawn *victim;
	struct point target, field;

	target = pawn->target.field;

	// Check central field for pawns.

	victims->central_count = 0;

	if (victim = pawns[target.y][target.x])
	{
		victims->central = victim;
		victims->central_count += 1;
	}

	// Check lateral fields for pawns.

	victims->lateral_count = 0;

	field = (struct point){target.x - 1, target.y};
	if ((field.x >= 0) && (victim = pawns[field.y][field.x]))
		victims->lateral[victims->lateral_count++] = victim;

	field = (struct point){target.x + 1, target.y};
	if ((field.x < BATTLEFIELD_WIDTH) && (victim = pawns[field.y][field.x]))
		victims->lateral[victims->lateral_count++] = victim;

	field = (struct point){target.x, target.y - 1};
	if ((field.y >= 0) && (victim = pawns[field.y][field.x]))
		victims->lateral[victims->lateral_count++] = victim;

	field = (struct point){target.x, target.y + 1};
	if ((field.y < BATTLEFIELD_HEIGHT) && (victim = pawns[field.y][field.x]))
		victims->lateral[victims->lateral_count++] = victim;

	// Check diagonal fields for pawns.

	victims->diagonal_count = 0;

	field = (struct point){target.x + 1, target.y - 1};
	if ((field.x < BATTLEFIELD_WIDTH) && (field.y >= 0) && (victim = pawns[field.y][field.x]))
		victims->diagonal[victims->diagonal_count++] = victim;

	field = (struct point){target.x - 1, target.y - 1};
	if ((field.x >= 0) && (field.y >= 0) && (victim = pawns[field.y][field.x]))
		victims->diagonal[victims->diagonal_count++] = victim;

	field = (struct point){target.x - 1, target.y + 1};
	if ((field.x >= 0) && (field.y < BATTLEFIELD_HEIGHT) && (victim = pawns[field.y][field.x]))
		victims->diagonal[victims->diagonal_count++] = victim;

	field = (struct point){target.x + 1, target.y + 1};
	if ((field.x < BATTLEFIELD_WIDTH) && (field.y < BATTLEFIELD_HEIGHT) && (victim = pawns[field.y][field.x]))
		victims->diagonal[victims->diagonal_count++] = victim;
}*/

static double battle_state_rating(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	size_t i, j;

	const struct pawn *pawns_moved[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH] = {0};

	double rating = 0.0;

	const struct pawn *victims[4];
	unsigned victims_count;

	// WARNING: We assume the locations, where the pawns are commanded to go, will be non-occupied.

	// Estimate which pawn will occupy a given location after the move.
	for(i = 0; i < battle->pawns_count; ++i)
	{
		const struct pawn *restrict pawn = battle->pawns + i;
		struct point location;

		if (!pawn->count) continue;

		if (pawn->troop->owner == player) location = pawn->moves[pawn->moves_count - 1].location;
		else location = pawn->moves[0].location; // TODO do better estimation here
		pawns_moved[location.y][location.x] = pawn;
	}

	// TODO the code below seems to have very high asymptotic complexity

	// TODO don't run away from range units unless you're faster

	// Estimate how beneficial is the command given to each of the player's pawns.
	for(i = 0; i < battle->players[player].pawns_count; ++i) // loop the pawns the player controls
	{
		const struct pawn *restrict pawn = battle->players[player].pawns[i];

		if (!pawn->count) continue;

		if (pawn->action == PAWN_SHOOT)
		{
			// estimate shoot impact
			// TODO this doesn't account for accuracy and damage spreading to nearby targets
			const struct pawn *victim = pawn->target.pawn;
			rating += unit_importance(battle, victim->troop->unit) * damage_expected(pawn, pawn->count, victim);
		}
		else if ((pawn->action == PAWN_ASSAULT) && battlefield_neighbors(pawn->moves[pawn->moves_count - 1].location, pawn->target.field))
		{
			// estimate assault impact
			// TODO
		}
		else
		{
			// estimate fight impact
			victims_count = victims_fight_find(game, pawn, player, pawns_moved, victims);
			for(j = 0; j < victims_count; ++j)
				rating += unit_importance(battle, victims[j]->troop->unit) * damage_expected(pawn, (double)pawn->count / victims_count, victims[j]);
		}

		// TODO take ally pawns into account

		// Estimate benefits and dangers for the following turns (moving, fighting, shooting, assault).
		for(j = 0; j < battle->pawns_count; ++j) // loop through enemy pawns
		{
			const struct pawn *restrict other = battle->pawns + j;
			const struct unit *restrict attacker = pawn->troop->unit;
			int status;
			double distance;

			if (!pawn->count) continue;
			if (allies(game, other->troop->owner, player)) continue;

			// TODO take into account obstacles on the way and damage splitting to neighboring fields
			if (attacker->ranged.damage && (round(battlefield_distance(pawn->moves[pawn->moves_count - 1].location, other->moves[0].location)) < attacker->ranged.range))
				rating += unit_importance(battle, other->troop->unit) * damage_expected_ranged(pawn, pawn->count, other);

			status = path_distance(pawn->moves[pawn->moves_count - 1].location, other->moves[0].location, graph, obstacles, &distance);
			if (status < 0) ; // TODO memory error
			if (distance == INFINITY) continue; // TODO this information is available through reachable

			// TODO use a better formula here
			rating -= unit_importance(battle, pawn->troop->unit) * damage_expected(other, other->count, pawn) * other->troop->unit->speed / distance;
			rating += unit_importance(battle, other->troop->unit) * damage_expected(pawn, pawn->count, other) * pawn->troop->unit->speed / distance;

			// TODO this is supposed to make the computer prefer setting the action; is this the right way?
			if ((pawn->action == PAWN_FIGHT) && (pawn->target.pawn == other))
				rating += unit_importance(battle, other->troop->unit) * damage_expected(pawn, pawn->count, other) * pawn->troop->unit->speed / distance;

			// TODO assault evaluation
		}

		// TODO walls close to the current position (if on the two diagonals, large bonus (gate blocking))
	}

	// Estimate how bad will the damage effects from enemies be.
	for(i = 0; i < battle->pawns_count; ++i) // loop enemy pawns
	{
		const struct pawn *restrict pawn = battle->pawns + i;

		if (!pawn->count) continue;
		if (allies(game, pawn->troop->owner, player)) continue;

		// TODO guess when a pawn will prefer shoot/assault or fighting specific target

		victims_count = victims_fight_find(game, pawn, player, pawns_moved, victims);
		for(j = 0; j < victims_count; ++j)
		{
			if (victims[j]->troop->owner != player) continue;
			rating -= unit_importance(battle, victims[j]->troop->unit) * damage_expected(pawn, (double)pawn->count / victims_count, victims[j]);
		}
	}

	return rating;
}

int computer_formation(const struct game *restrict game, struct battle *restrict battle, unsigned char player)
{
	// TODO
	return 0;
}

// Decide the behavior of the computer using simulated annealing.
int computer_battle(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	unsigned step;
	size_t i;

	size_t pawns_count = battle->players[player].pawns_count;
	struct pawn_command *backup;
	double (*reachable)[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];

	int status = 0;

	double rating, rating_new;

	reachable = malloc(pawns_count * sizeof(*reachable));
	if (!reachable) return ERROR_MEMORY;
	backup = malloc(offsetof(struct pawn_command, moves) + 32 * sizeof(struct move)); // TODO fix this 32
	if (!backup)
	{
		free(reachable);
		return ERROR_MEMORY;
	}

	// Cancel pawn actions and determine the reachable fields for each pawn.
	for(i = 0; i < pawns_count; ++i)
	{
		struct pawn *restrict pawn = battle->players[player].pawns[i];

		pawn->action = 0;
		pawn->moves_count = 1;

		status = path_distances(pawn, graph, obstacles, reachable[i]);
		if (status < 0) goto finally;
	}

	// Choose suitable commands for the pawns of the player.
	rating = battle_state_rating(game, battle, player, graph, obstacles);
	printf(">>\n");
	for(step = 0; step < SEARCH_TRIES; ++step) // TODO think about this
	{
		struct pawn *restrict pawn;

		printf(">> rating: %f\n", rating);

		i = random() % pawns_count;
		pawn = battle->players[player].pawns[i];

		// Remember current pawn command.
		backup->action = pawn->action;
		if (pawn->action == PAWN_FIGHT) backup->target = pawn->target.pawn->moves[0].location;
		else backup->target = pawn->target.field;
		backup->moves_count = pawn->moves_count;
		memcpy(backup->moves, pawn->moves, pawn->moves_count * sizeof(*pawn->moves));

		// TODO ? sequentially generate neighboring states (instead of generating one state and trying to switch to it)

		battle_state_change(game, battle, pawn, graph, obstacles, reachable[i]);

		printf("%d,%d -> %d,%d\n", pawn->moves[0].location.x, pawn->moves[0].location.y, pawn->moves[pawn->moves_count - 1].location.x, pawn->moves[pawn->moves_count - 1].location.y);

		// Calculate the rating of the new set of commands.
		// Revert the new command if it is unacceptably worse than the current one.
		// TODO fix temperature calculation
		rating_new = battle_state_rating(game, battle, player, graph, obstacles);
		if (state_wanted(rating, rating_new, SEARCH_TRIES - step)) rating = rating_new;
		else
		{
			pawn->action = backup->action;
			if (backup->action == PAWN_FIGHT) pawn->target.pawn = battle->field[backup->target.y][backup->target.x].pawn;
			else pawn->target.field = backup->target;
			pawn->moves_count = backup->moves_count;
			memcpy(pawn->moves, backup->moves, backup->moves_count * sizeof(*backup->moves));

			printf("skipped: %f\n", rating_new);
		}
	}
	printf("final rating: %f\n", rating);

finally:
	free(backup);
	free(reachable);

	return status;
}
