#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "combat.h"
#include "computer.h"

/*
struct unit
{
	uint32_t requires;
	unsigned troops_count;
	struct resources cost, expense;
	unsigned char time;

	unsigned char speed;
	unsigned char health;
	enum armor armor;

	struct
	{
		enum weapon weapon;
		double damage;
		double agility;
	} melee;
	struct
	{
		enum weapon weapon;
		double damage;
		unsigned char range;
	} ranged;
};
*/
/*
int combat_order_fight(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct pawn *restrict victim);
int combat_order_assault(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct point target);
int combat_order_shoot(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict shooter, struct point target);
int movement_queue(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes, const struct obstacles *restrict obstacles);
*/

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

	//
}

static unsigned pawn_victims(const struct game *restrict game, const struct pawn *pawns[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], const struct pawn *restrict pawn, const struct pawn_command *restrict command, const struct pawn *restrict victims[static 4])
{
	const struct pawn *victim;
	unsigned victims_count = 0;

	struct point destination = command->moves[command->moves_count - 1].location;

	// TODO what if one of the pawns is on a tower

	// If the pawn has a specific fight target and is able to fight it, fight only that target.
	if ((command->action == PAWN_FIGHT) && battlefield_neighbors(destination, command->target))
	{
		victims[victims_count++] = pawns[command->target.y][command->target.x];
	}
	else
	{
		int x = destination.x;
		int y = destination.y;

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

static double state_assess(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct pawn_command **restrict commands)
{
	size_t i, j;

	const struct pawn *pawns_moved[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH] = {0};

	double rating = 0.0;

	const struct pawn *victims[4];
	unsigned victims_count;

	struct pawn_command *command = malloc(offsetof(struct pawn_command, moves) + sizeof(*command->moves));

	// WARNING: We assume the locations where the pawns are commanded to go will be non-occupied.

	// Estimate which pawn will occupy a given location after the move.
	for(i = 0; i < battle->pawns_count; ++i)
	{
		const struct pawn *restrict pawn = battle->pawns + i;
		struct point location;

		if (!pawn->count) continue;

		if (pawn->troop->owner == player) location = commands[i]->moves[commands[i]->moves_count - 1].location;
		else location = pawn->moves[0].location; // TODO do better estimation here
		pawns_moved[location.y][location.x] = pawn;
	}

	// Estimate how beneficial is the command given to each of the player's pawns.
	for(i = 0; i < battle->players[player].pawns_count; ++i) // loop the pawns the player controls
	{
		const struct pawn *restrict pawn = battle->players[i].pawns[i];
		const struct pawn_command *restrict command = commands[i];

		if (!pawn->count) continue;

		if (command->action == PAWN_SHOOT)
		{
			// estimate shoot impact
			// TODO
		}
		else if ((command->action == PAWN_ASSAULT) && battlefield_neighbors(command->moves[command->moves_count - 1].location, command->target))
		{
			// estimate assault impact
			// TODO
		}
		else
		{
			// estimate fight impact

			victims_count = pawn_victims(game, pawns_moved, pawn, command, victims);

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
		command->action = 0;
		command->moves_count = 1;
		command->moves[0] = pawn->moves[0];

		victims_count = pawn_victims(game, pawns_moved, pawn, command, victims);
		for(j = 0; j < victims_count; ++j)
			rating -= damage_expected(pawn, (double)pawn->count / victims_count, victims[j]) * unit_importance(battle, victims[j]->troop->unit);
	}

	free(command);

	return 1.0;
}

int computer_formation(const struct game *restrict game, struct battle *restrict battle, unsigned char player)
{
	//

	return 0;
}

static void state_change(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct pawn_command *restrict command, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	//
}

// Decide the behavior of the comuter using simulated annealing.
int computer_battle(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	unsigned step;
	size_t i;

	size_t pawns_count = battle->players[player].pawns_count;
	struct pawn_command **commands, *backup;

	int status;

	double rate, rate_new;

	commands = malloc(pawns_count * sizeof(*commands));
	if (!commands) return ERROR_MEMORY;
	for(i = 0; i < pawns_count; ++i)
	{
		struct pawn *restrict pawn = battle->players[player].pawns[i];

		commands[i] = malloc(offsetof(struct pawn_command, moves) + 32 * sizeof(struct move)); // TODO fix this 32
		if (!commands[i])
		{
			status = ERROR_MEMORY;
			goto finally;
		}

		commands[i]->action = 0;

		commands[i]->moves_count = 1;
		commands[i]->moves[0] = pawn->moves[0];
	}
	backup = malloc(offsetof(struct pawn_command, moves) + 32 * sizeof(struct move)); // TODO fix this 32

	// TODO generate reachable for each pawn (is this necessary since it can change?)

	// Choose suitable commands for the pawns of the player.
	rate = state_assess(game, battle, player, commands);
	for(step = 0; step < SEARCH_TRIES; ++step) // TODO think about this
	{
		struct pawn_command *restrict command = commands[i];

		i = random() % pawns_count;

		backup->action = command->action;
		backup->target = command->target;
		backup->moves_count = command->moves_count;
		memcpy(backup->moves, command->moves, command->moves_count);

		state_change(game, battle, battle->players[player].pawns[i], commands[i], graph, obstacles);

		// Calculate the rate of the new set of commands.
		// Revert the new command if it is unacceptably worse than the current one.
		rate_new = state_assess(game, battle, player, commands);
		if (rate_new + (SEARCH_TRIES - step - 1) < rate)
		{
			struct pawn_command *swap = command;
			command = backup;
			backup = swap;
		}
	}

	// Apply the chosen commands to the pawns.
	for(i = 0; i < pawns_count; ++i)
	{
		struct pawn *restrict pawn = battle->players[player].pawns[i];
		struct pawn_command *restrict command = commands[i];

		pawn->action = command->action;
		switch (command->action)
		{
		case PAWN_FIGHT:
			pawn->target.pawn = battle->field[command->target.y][command->target.x].pawn;
			break;

		case PAWN_ASSAULT:
		case PAWN_SHOOT:
			pawn->target.field = command->target;
			break;
		}

		pawn->moves_count = command->moves_count;
		memcpy(pawn->moves, command->moves, command->moves_count);
	}

	free(backup);
finally:
	while (i--) free(commands[i]);
	free(commands);

	return status;
}
