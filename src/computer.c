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

#define MAP_COMMAND_PRIORITY_THRESHOLD 0.0 /* TODO maybe this should not be a macro */

struct region_command
{
	struct region *region;
	enum {COMMAND_BUILD, COMMAND_TRAIN} type;
	union
	{
		uint32_t building;
		size_t unit;
	} target;
	double priority;
};

#define heap_type struct region_command *
#define heap_above(a, b) ((a)->priority >= (b)->priority)
#include "generic/heap.g"

struct pawn_command
{
	enum pawn_action action;
	struct point target;

	size_t moves_count;
	struct move moves[];
};

//enum {SEARCH_TRIES = 1048576};
enum {SEARCH_TRIES = 256};

static double unit_importance(const struct battle *restrict battle, const struct unit *restrict unit)
{
	// TODO better implementation for this
	// TODO importance should depend on battle obstacles (e.g. the more they are, the more important is battering ram)

	return unit->health + unit->melee.damage * 2 + unit->ranged.damage * 3;
}

static void state_change(const struct game *restrict game, const struct battle *restrict battle, struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	unsigned speed = pawn->troop->unit->speed;
	struct point location = pawn->moves[pawn->moves_count - 1].location, target;

	struct point neighbors[4];
	unsigned neighbors_count = 0;

	// int path_distance(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, double *restrict distance);

	/* TODO support actions other than just move
	int combat_order_fight(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct pawn *restrict victim);
	int combat_order_assault(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct point target);
	int combat_order_shoot(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict shooter, struct point target);
	int movement_queue(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes, const struct obstacles *restrict obstacles);
	*/

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
		pawn->moves_count = 1;
		movement_queue(pawn, neighbor, graph, obstacles);
	}
}

static unsigned pawn_victims(const struct game *restrict game, const struct pawn *restrict pawn, unsigned char player, const struct pawn *pawns[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], const struct pawn *restrict victims[static 4])
{
	const struct pawn *victim;
	unsigned victims_count = 0;

	struct point location, target;

	// TODO what if one of the pawns is on a tower

	if (pawn->troop->owner == player)
	{
		location = pawn->moves[pawn->moves_count - 1].location;

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

static double state_assess(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	size_t i, j;

	const struct pawn *pawns_moved[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH] = {0};

	double rating = 0.0;

	const struct pawn *victims[4];
	unsigned victims_count;

	// WARNING: We assume the locations where the pawns are commanded to go will be non-occupied.

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

	// Estimate how beneficial is the command given to each of the player's pawns.
	for(i = 0; i < battle->players[player].pawns_count; ++i) // loop the pawns the player controls
	{
		const struct pawn *restrict pawn = battle->players[player].pawns[i];

		if (!pawn->count) continue;

		if (pawn->action == PAWN_SHOOT)
		{
			// estimate shoot impact
			// TODO
		}
		else if ((pawn->action == PAWN_ASSAULT) && battlefield_neighbors(pawn->moves[pawn->moves_count - 1].location, pawn->target.field))
		{
			// estimate assault impact
			// TODO
		}
		else
		{
			// estimate fight impact

			victims_count = pawn_victims(game, pawn, player, pawns_moved, victims);

			for(j = 0; j < victims_count; ++j)
				rating += damage_expected(pawn, (double)pawn->count / victims_count, victims[j]) * unit_importance(battle, victims[j]->troop->unit);
		}

		// Estimate the possibilities for the following turns (moving, fighting, shooting, assault).
		for(j = 0; j < battle->pawns_count; ++j) // loop enemy pawns
		{
			const struct pawn *restrict other = battle->pawns + j;
			int status;
			double distance;

			if (!pawn->count) continue;
			if (allies(game, other->troop->owner, player)) continue;

			status = path_distance(pawn, other->moves[0].location, graph, obstacles, &distance);
			if (status < 0) ; // TODO memory error
			if (distance == INFINITY) continue; // TODO this information is available through reachable
			//if (reachable[i][other->moves[0].location.y][other->moves[0].location.x] == INFINITY) continue;
			if (distance <= (1.0 + FLOAT_PRECISION)) continue; // ignore neighbors

			rating += unit_importance(battle, other->troop->unit) * pawn->troop->unit->speed / distance;

			// TODO support shoot and assault evaluations
		}

		// TODO walls close to the current position (if on the two diagonals, large bonus (gate blocking))
	}

	// TODO take ally pawns into account

	// Estimate how bad will the damage effects from enemies be.
	for(i = 0; i < battle->pawns_count; ++i) // loop enemy pawns
	{
		const struct pawn *restrict pawn = battle->pawns + i;

		if (!pawn->count) continue;
		if (allies(game, pawn->troop->owner, player)) continue;

		// TODO guess when a pawn will prefer shoot/assault or fighting specific target

		victims_count = pawn_victims(game, pawn, player, pawns_moved, victims);
		for(j = 0; j < victims_count; ++j)
			rating -= damage_expected(pawn, (double)pawn->count / victims_count, victims[j]) * unit_importance(battle, victims[j]->troop->unit);

		// TODO estimate how the enemy can damage the player's pawns; the more the enemies and the fewer the player's pawns, the worse
	}

	return rating;
}

void computer_map_perform(struct heap *restrict commands, const struct game *restrict game, unsigned char player)
{
	size_t i;

	while (commands->count)
	{
		struct region_command *command = commands->data[0];
		heap_pop(commands);

		if (command->priority < MAP_COMMAND_PRIORITY_THRESHOLD)
			break;

		switch (command->type)
		{
		case COMMAND_BUILD:
			if (!resource_enough(&game->players[player].treasury, &buildings[command->target.building].cost))
				break;
			if (command->region->construct >= 0)
				break;
			resource_subtract(&game->players[player].treasury, &buildings[command->target.building].cost);
			command->region->construct = command->target.building;
			break;

		case COMMAND_TRAIN:
			if (!resource_enough(&game->players[player].treasury, &UNITS[command->target.unit].cost))
				break;
			for(i = 0; i < TRAIN_QUEUE; ++i)
				if (!command->region->train[i])
					goto train;
			break;
		train:
			resource_subtract(&game->players[player].treasury, &UNITS[command->target.unit].cost);
			command->region->train[i] = UNITS + command->target.unit;
			break;
		}
	}
}

int computer_map(const struct game *restrict game, unsigned char player)
{
	// TODO support cancelling buildings and trainings

	size_t i;

	size_t commands_allocated = 8, commands_count = 0; // TODO fix this 8
	struct region_command *commands = malloc(commands_allocated * sizeof(*commands));
	if (!commands)
		return ERROR_MEMORY;

	// Make a list of the commands available for the player.
	for(i = 0; i < game->regions_count; ++i)
	{
		// TODO add the commands
	}

	if (!commands_count)
	{
		free(commands);
		return 0;
	}

	// Create a priority queue with the available commands.
	struct heap commands_queue;
	commands_queue.count = commands_count;
	commands_queue.data = malloc(commands_count * sizeof(*commands_queue.data));
	if (!commands_queue.data)
	{
		free(commands);
		return ERROR_MEMORY;
	}
	for(i = 0; i < commands_count; ++i)
		commands_queue.data[i] = commands + i;
	heap_heapify(&commands_queue);

	computer_map_perform(&commands_queue, game, player);

	free(commands_queue.data);
	free(commands);

	return 0;
}

int computer_formation(const struct game *restrict game, struct battle *restrict battle, unsigned char player)
{
	// TODO
	return 0;
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

	double rate, rate_new;

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
	rate = state_assess(game, battle, player, graph, obstacles);
	for(step = 0; step < SEARCH_TRIES; ++step) // TODO think about this
	{
		struct pawn *restrict pawn;

		i = random() % pawns_count;
		pawn = battle->players[player].pawns[i];

		backup->action = pawn->action;
		if (pawn->action == PAWN_FIGHT) backup->target = pawn->target.pawn->moves[0].location;
		else backup->target = pawn->target.field;
		backup->moves_count = pawn->moves_count;
		memcpy(backup->moves, pawn->moves, pawn->moves_count * sizeof(*pawn->moves));

		state_change(game, battle, pawn, graph, obstacles, reachable[i]);

		// Calculate the rate of the new set of commands.
		// Revert the new command if it is unacceptably worse than the current one.
		rate_new = state_assess(game, battle, player, graph, obstacles);
		if (rate_new + (SEARCH_TRIES - step - 1) < rate) // TODO this should be a comparison between function returning probability and random()
		{
			pawn->action = backup->action;
			if (backup->action == PAWN_FIGHT) pawn->target.pawn = battle->field[backup->target.y][backup->target.x].pawn;
			else pawn->target.field = backup->target;
			pawn->moves_count = backup->moves_count;
			memcpy(pawn->moves, backup->moves, backup->moves_count * sizeof(*backup->moves));
		}
	}

finally:
	free(backup);
	free(reachable);

	return status;
}
