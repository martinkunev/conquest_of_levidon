#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "combat.h"
#include "movement.h"
#include "computer.h"

struct pawn_command
{
	enum pawn_action action;
	struct point target;

	size_t moves_count;
	struct move moves[];
};

enum {SEARCH_TRIES = 1048576};

// TODO importance should depend on battle obstacles (e.g. the more they are, the more important is battering ram)
static double unit_importance(const struct battle *restrict battle, const struct unit *restrict unit)
{
	return 1.0;

	// TODO return unit->health;
	//
}

static void state_change(const struct game *restrict game, const struct battle *restrict battle, struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	unsigned speed = pawn->troop->unit->speed;
	struct point location = pawn->moves[pawn->moves_count - 1].location, target;

	struct point neighbors[4];
	unsigned neighbors_count = 0;

	/* TODO support actions other than just move
	int combat_order_fight(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct pawn *restrict victim);
	int combat_order_assault(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct point target);
	int combat_order_shoot(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict shooter, struct point target);
	int movement_queue(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes, const struct obstacles *restrict obstacles);
	*/

	target.x = (int)location.x - 1;
	target.y = location.y;
	if ((target.x >= 0) && (speed >= reachable[target.y][target.x]) && battlefield_passable(game, &battle->field[target.y][target.x], pawn->troop->owner))
		neighbors[neighbors_count++] = target;

	target.x = (int)location.x + 1;
	target.y = location.y;
	if ((target.x < BATTLEFIELD_WIDTH) && (speed >= reachable[target.y][target.x]) && battlefield_passable(game, &battle->field[target.y][target.x], pawn->troop->owner))
		neighbors[neighbors_count++] = target;

	target.x = location.x;
	target.y = (int)location.y - 1;
	if ((target.y >= 0) && (speed >= reachable[target.y][target.x]) && battlefield_passable(game, &battle->field[target.y][target.x], pawn->troop->owner))
		neighbors[neighbors_count++] = target;

	target.x = location.x;
	target.y = (int)location.y + 1;
	if ((target.y < BATTLEFIELD_HEIGHT) && (speed >= reachable[target.y][target.x]) && battlefield_passable(game, &battle->field[target.y][target.x], pawn->troop->owner))
		neighbors[neighbors_count++] = target;

	if (neighbors_count)
	{
		struct point neighbor = neighbors[random() % neighbors_count];
		pawn->moves_count = 1;
		movement_queue(pawn, neighbor, graph, obstacles);
	}
}

static unsigned pawn_victims(const struct game *restrict game, const struct pawn *pawns[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], const struct pawn *restrict pawn, struct point location, unsigned char player, const struct pawn *restrict victims[static 4])
{
	const struct pawn *victim;
	unsigned victims_count = 0;

	// TODO what if one of the pawns is on a tower

	// TODO: ugly; too many arguments; too hard to follow what arguments are necessary

	// If the pawn has a specific fight target and is able to fight it, fight only that target.
	if ((pawn->troop->owner == player) && (pawn->action == PAWN_FIGHT) && battlefield_neighbors(location, pawn->target.pawn->moves[0].location))
	{
		victims[victims_count++] = pawn->target.pawn;
	}
	else
	{
		int x = location.x;
		int y = location.y;

		// Look for pawns to fight at the neighboring fields.
		if ((x > 0) && (victim = pawns[y][x - 1]) && !allies(game, pawn->troop->owner, victim->troop->owner))
			victims[victims_count++] = victim;
		if ((x < (BATTLEFIELD_WIDTH - 1)) && (victim = pawns[y][x + 1]) && !allies(game, pawn->troop->owner, victim->troop->owner))
			victims[victims_count++] = victim;
		if ((y > 0) && (victim = pawns[y - 1][x]) && !allies(game, pawn->troop->owner, victim->troop->owner))
			victims[victims_count++] = victim;
		if ((y < (BATTLEFIELD_HEIGHT - 1)) && (victim = pawns[y + 1][x]) && !allies(game, pawn->troop->owner, victim->troop->owner))
			victims[victims_count++] = victim;
	}

	return victims_count;
}

static double state_assess(const struct game *restrict game, struct battle *restrict battle, unsigned char player)
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

			victims_count = pawn_victims(game, pawns_moved, pawn, pawn->moves[pawn->moves_count - 1].location, player, victims);

			for(j = 0; j < victims_count; ++j)
				rating += damage_expected(pawn, (double)pawn->count / victims_count, victims[j]) * unit_importance(battle, victims[j]->troop->unit);
		}

		// TODO increase rating on future possibility of shooting, assault and attack

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

		victims_count = pawn_victims(game, pawns_moved, pawn, pawn->moves[0].location, player, victims);
		for(j = 0; j < victims_count; ++j)
			rating -= damage_expected(pawn, (double)pawn->count / victims_count, victims[j]) * unit_importance(battle, victims[j]->troop->unit);
	}

	return 1.0;
}

int computer_formation(const struct game *restrict game, struct battle *restrict battle, unsigned char player)
{
	//

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

		status = path_reachable(pawn, graph, obstacles, reachable[i]);
		if (status < 0) goto finally;
	}

	// Choose suitable commands for the pawns of the player.
	//rate = state_assess(game, battle, player, commands);
	rate = state_assess(game, battle, player);
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
		rate_new = state_assess(game, battle, player);
		if (rate_new + (SEARCH_TRIES - step - 1) < rate)
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
