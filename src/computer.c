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

#include <stdio.h>

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

//enum {SEARCH_TRIES = 1024};
enum {SEARCH_TRIES = 64};

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

static int computer_map_orders_list(struct array_orders *restrict orders, const struct game *restrict game, unsigned char player, unsigned char regions_visible[REGIONS_LIMIT])
{
	size_t i, j;

	if (array_orders_init(orders, 8) < 0) // TODO fix this 8
		return ERROR_MEMORY;

	// Make a list of the orders available for the player.
	for(i = 0; i < game->regions_count; ++i)
	{
		struct region *restrict region = game->regions + i;

		unsigned neighbors_enemy = 0;

		if ((region->owner != player) || (region->garrison.owner != player)) continue;

		for(j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			const struct region *restrict neighbor = region->neighbors[j];

			if (!neighbor) continue;
			if (!regions_visible[neighbor->index]) continue;

			if ((region->owner != PLAYER_NEUTRAL) && !allies(game, player, region->owner))
				neighbors_enemy += 1;
		}

		if (region->construct < 0) // no construction in progress
		{
			for(j = 0; j < buildings_count; ++j)
			{
				if (region_built(region, j)) continue;
				if (!region_building_available(region, buildings[j])) continue;

				// Don't check if there are enough resources as the first performed order will invalidate the check.

				if (array_orders_expand(orders, orders->count + 1) < 0)
					goto error;
				orders->data[orders->count].region = region;
				orders->data[orders->count].type = ORDER_BUILD;
				orders->data[orders->count].target.building = j;
				orders->data[orders->count].priority = desire_buildings[j] / (neighbors_enemy + 1); // TODO this is more complicated
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
				orders->data[orders->count].region = region;
				orders->data[orders->count].type = ORDER_TRAIN;
				orders->data[orders->count].target.unit = j;
				orders->data[orders->count].priority = desire_units[j] * neighbors_enemy; // TODO this is more complicated
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
	size_t i;

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
			break;

		case ORDER_TRAIN:
			if (!resource_enough(&game->players[player].treasury, &UNITS[order->target.unit].cost))
				break;
			for(i = 0; i < TRAIN_QUEUE; ++i)
				if (!order->region->train[i])
					goto train;
			break;
		train:
			resource_subtract(&game->players[player].treasury, &UNITS[order->target.unit].cost);
			order->region->train[i] = UNITS + order->target.unit;
			break;
		}
	}
}

int computer_map(const struct game *restrict game, unsigned char player)
{
	// TODO support cancelling buildings and trainings

	// TODO troop movement in/out of garrison and between regions

	size_t i;

	unsigned char regions_visible[REGIONS_LIMIT];
	struct array_orders orders;

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

	// Move troops between regions.
	/*for(i = 0; i < game->regions_count; ++i)
	{
		unsigned neighbors_neutral = 0, neighbors_ally = 0, neighbors_enemy = 0, neighbors_sieging = 0, neighbors_sieged = 0;
		//const struct region *ne

		const struct region *restrict region = game->regions + i;

		if (region->owner != player) continue;
		if (!region->troops) continue;

		// Determine what is the most important thing to do with region troops.
		for(j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			const struct region *neighbor = region->neighbors[j];

			if (!neighbor) continue;
			if (!regions_visible[neighbor->index]) continue;

			if (neighbor->owner == PLAYER_NEUTRAL) // neutral region
			{
				neighbors_neutral += 1;
			}
			else if (!allies(game, player, neighbor->owner)) // enemy region
			{
				if (allies(game, player, neighbor->garrison.owner)) neighbors_sieged += 1;
				else neighbors_enemy += 1;
			}
			else if (allies(game, player, neighbor->garrison.owner)) // self/ally region without siege
			{
				neighbors_ally += 1;
			}
			else if (player == neighbor->owner) // current player is sieging the region
			{
				neighbors_sieging += 1;
			}
		}

		if (neighbors_sieged)
		{
			for(troop = region->troops; troop; troop = troop->_next)
			{
				//
			}
		}
		else if (neighbors_sieging)
		{
		}
		else if (neighbors_enemy)
		{
		}
		else if (neighbors_neutral)
		{
		}
		else if (neighbors_ally)
		{
		}
	}*/

	return 0;
}

static double unit_importance(const struct battle *restrict battle, const struct unit *restrict unit)
{
	// Peasant: 7
	// Militia: 9
	// Pikeman: 10
	// Archer: 8.5
	// Longbow: 13
	// Light cavalry: 14
	// Battering ram: 72.5

	// TODO more sophisticated logic here
	// TODO importance should depend on battle obstacles (e.g. the more they are, the more important is battering ram)

	return unit->health + unit->melee.damage * unit->melee.agility * 2 + unit->ranged.damage * 3;
}

static void state_change(const struct game *restrict game, const struct battle *restrict battle, struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	unsigned speed = pawn->troop->unit->speed;
	struct point location = pawn->moves[pawn->moves_count - 1].location, target;

	struct point neighbors[4];
	unsigned neighbors_count = 0;

	// TODO don't change just movement

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

	if (neighbors_count)
	{
		struct point neighbor = neighbors[random() % neighbors_count];
		const struct pawn *restrict target_pawn = battle->field[neighbor.y][neighbor.x].pawn;

		if (!target_pawn || !combat_order_fight(game, battle, obstacles, pawn, target_pawn))
		{
			pawn->moves_count = 1;
			movement_queue(pawn, neighbor, graph, obstacles);
		}
	}
}

static unsigned pawn_victims_fight(const struct game *restrict game, const struct pawn *restrict pawn, unsigned char player, const struct pawn *pawns[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], const struct pawn *restrict victims[static 4])
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

/*struct combat_victims
{
	struct pawn *victims[9];
	unsigned damage[9];
	size_t count;
};*/

/*static unsigned pawn_victims_shoot(const struct pawn *restrict pawn, const struct pawn *pawns[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct combat_victims *restrict victims)
{
	const double targets[3][2] = {{1, 0.5}, {0, 0.078125}, {0, 0.046875}}; // 1/2, 5/64, 3/64

	// There is friendly fire.

	const struct pawn *victim;
	unsigned victims_count = 0;

	// TODO what if one of the pawns is on a tower

	struct point target, field;
	target = pawn->target.field;

	////

	unsigned target_index;
	double miss, on_target;
	unsigned range;

	double damage_total = shooter->troop->unit->ranged.damage;
	unsigned damage;

	range = shooter->troop->unit->ranged.range;
	if (!target_visible(shooter->moves[0].location, shooter->target.field, obstacles))
	{
		damage_total *= ACCURACY;
		range -= 1;
	}
	miss = battlefield_distance(shooter->moves[0].location, shooter->target.field) / range;

	////

	// Shooters have some chance to hit a field adjacent to the target, depending on the distance.
	// Damage is dealt to the target field and to its neighbors.

	target_index = 0;
	on_target = targets[target_index][0] * (1 - miss) + targets[target_index][1] * miss;
	damage = (unsigned)(damage_total * on_target + 0.5);
	damage_shoot(shooter, battle->field[y][x].pawn, damage);

	target_index = 1;
	on_target = targets[target_index][0] * (1 - miss) + targets[target_index][1] * miss;
	damage = (unsigned)(damage_total * on_target + 0.5);
	if (x > 0) damage_shoot(shooter, battle->field[y][x - 1].pawn, damage);
	if (x < (BATTLEFIELD_WIDTH - 1)) damage_shoot(shooter, battle->field[y][x + 1].pawn, damage);
	if (y > 0) damage_shoot(shooter, battle->field[y - 1][x].pawn, damage);
	if (y < (BATTLEFIELD_HEIGHT - 1)) damage_shoot(shooter, battle->field[y + 1][x].pawn, damage);

	target_index = 2;
	on_target = targets[target_index][0] * (1 - miss) + targets[target_index][1] * miss;
	damage = (unsigned)(damage_total * on_target + 0.5);
	if (x > 0)
	{
		if (y > 0) damage_shoot(shooter, battle->field[y - 1][x - 1].pawn, damage);
		if (y < (BATTLEFIELD_HEIGHT - 1)) damage_shoot(shooter, battle->field[y + 1][x - 1].pawn, damage);
	}
	if (x < (BATTLEFIELD_WIDTH - 1))
	{
		if (y > 0) damage_shoot(shooter, battle->field[y - 1][x + 1].pawn, damage);
		if (y < (BATTLEFIELD_HEIGHT - 1)) damage_shoot(shooter, battle->field[y + 1][x + 1].pawn, damage);
	}

	////


	// Look for pawns at the target field and the fields around it.

	// TODO different victims take different amounts of damage

	if (victim = pawns[target.y][target.x])
		victims[victims_count++] = victim;

	field = (struct point){target.x - 1, target.y};
	if ((field.x >= 0) && (victim = pawns[field.y][field.x]))
		victims[victims_count++] = victim;

	field = (struct point){target.x + 1, target.y};
	if ((field.x < BATTLEFIELD_WIDTH) && (victim = pawns[field.y][field.x]))
		victims[victims_count++] = victim;

	field = (struct point){target.x, target.y - 1};
	if ((field.y >= 0) && (victim = pawns[field.y][field.x]))
		victims[victims_count++] = victim;

	field = (struct point){target.x, target.y + 1};
	if ((field.y < BATTLEFIELD_HEIGHT) && (victim = pawns[field.y][field.x]))
		victims[victims_count++] = victim;

	// TODO diagonals

	return victims_count;
}*/

static double state_rating(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
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

	// TODO the code below seems to have very high complexity

	// TODO don't run away from range units unless you're faster

	// Estimate how beneficial is the command given to each of the player's pawns.
	for(i = 0; i < battle->players[player].pawns_count; ++i) // loop the pawns the player controls
	{
		const struct pawn *restrict pawn = battle->players[player].pawns[i];

		if (!pawn->count) continue;

		if (pawn->action == PAWN_SHOOT)
		{
			// estimate shoot impact
			// TODO

			// pawn->target.field
		}
		else if ((pawn->action == PAWN_ASSAULT) && battlefield_neighbors(pawn->moves[pawn->moves_count - 1].location, pawn->target.field))
		{
			// estimate assault impact
			// TODO
		}
		else
		{
			// estimate fight impact
			victims_count = pawn_victims_fight(game, pawn, player, pawns_moved, victims);
			for(j = 0; j < victims_count; ++j)
				rating += unit_importance(battle, victims[j]->troop->unit) * damage_expected(pawn, (double)pawn->count / victims_count, victims[j]);
		}

		// TODO take ally pawns into account

		// Estimate benefits and dangers for the following turns (moving, fighting, shooting, assault).
		for(j = 0; j < battle->pawns_count; ++j) // loop through enemy pawns
		{
			const struct pawn *restrict other = battle->pawns + j;
			int status;
			double distance;

			if (!pawn->count) continue;
			if (allies(game, other->troop->owner, player)) continue;

			status = path_distance(pawn->moves[pawn->moves_count - 1].location, other->moves[0].location, graph, obstacles, &distance);
			if (status < 0) ; // TODO memory error
			if (distance == INFINITY) continue; // TODO this information is available through reachable

			// TODO use a better formula here
			rating -= unit_importance(battle, pawn->troop->unit) * damage_expected(other, other->count, pawn) * other->troop->unit->speed / distance;
			rating += unit_importance(battle, other->troop->unit) * damage_expected(pawn, pawn->count, other) * pawn->troop->unit->speed / distance;

			// TODO this is supposed to make the computer prefer setting the action; is this the right way?
			if ((pawn->action == PAWN_FIGHT) && (pawn->target.pawn == other))
				rating += unit_importance(battle, other->troop->unit) * damage_expected(pawn, pawn->count, other) * pawn->troop->unit->speed / distance;

			// TODO support shoot and assault evaluations
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

		victims_count = pawn_victims_fight(game, pawn, player, pawns_moved, victims);
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

static int state_wanted(double rate, double rate_new, unsigned step)
{
	// TODO this should be a comparison between function returning probability and random()

	return (rate_new + (SEARCH_TRIES - step - 1) > rate);
}

// Decide the behavior of the comuter using simulated annealing.
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
	rating = state_rating(game, battle, player, graph, obstacles);
	printf(">>\n", rating);
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

		// TODO sequentially generate neighboring states (instead of generating one state and trying to switch to it)

		state_change(game, battle, pawn, graph, obstacles, reachable[i]);

		printf("%d,%d -> %d,%d\n", pawn->moves[0].location.x, pawn->moves[0].location.y, pawn->moves[pawn->moves_count - 1].location.x, pawn->moves[pawn->moves_count - 1].location.y);

		// Calculate the rating of the new set of commands.
		// Revert the new command if it is unacceptably worse than the current one.
		rating_new = state_rating(game, battle, player, graph, obstacles);
		if (state_wanted(rating, rating_new, step)) rating = rating_new;
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
