#include <stdlib.h>

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

// TODO importance should depend on battle obstacles (e.g. the more they are, the more important is battering ram)
static double unit_importance(const struct battle *restrict battle, const struct unit *restrict unit)
{
	return 1.0;

	//
}

static unsigned pawn_victims(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, const struct pawn_command *restrict command, struct pawn *restrict victims[static 4])
{
	struct pawn *victim;
	unsigned victims_count = 0;

	struct point destination = command->moves[command->moves_count - 1].location;

	// TODO what if one of the pawns is on a tower

	// If the pawn has a specific fight target and is able to fight it, fight only that target.
	if ((command->action == PAWN_FIGHT) && battlefield_neighbors(destination, command->target))
	{
		victims[victims_count++] = battle->field[command->target.y][command->target.x].pawn;
	}
	else
	{
		int x = destination.x;
		int y = destination.y;

		// Look for pawns to fight at the neighboring fields.
		if ((x > 0) && (victim = battle->field[y][x - 1].pawn) && !allies(game, pawn->troop->owner, victim->troop->owner))
			victims[victims_count++] = victim;
		if ((x < (BATTLEFIELD_WIDTH - 1)) && (victim = battle->field[y][x + 1].pawn) && !allies(game, pawn->troop->owner, victim->troop->owner))
			victims[victims_count++] = victim;
		if ((y > 0) && (victim = battle->field[y - 1][x].pawn) && !allies(game, pawn->troop->owner, victim->troop->owner))
			victims[victims_count++] = victim;
		if ((y < (BATTLEFIELD_HEIGHT - 1)) && (victim = battle->field[y + 1][x].pawn) && !allies(game, pawn->troop->owner, victim->troop->owner))
			victims[victims_count++] = victim;
	}

	return victims_count;
}

/*
damage that the pawn can do is added (more if it's done to stronger units)
damage that is done to the pawn is subtracted in the same manner
possibility of shooting, assault and attack
closeness to allies
*/
static double state_assess(const struct game *restrict game, struct battle *restrict battle, unsigned char player, const struct pawn_command *restrict commands)
{
	size_t i, j;

	const struct pawn *pawns_moved[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH] = {0};

	double rating = 0.0;

	// WARNING: We assume the locations where the pawns are commanded to go will be non-occupied.

	// Estimate which pawn will occupy a given location after the move.
	for(i = 0; i < battle->players[i].pawns_count; ++i) // loop the pawns the player controls
	{
		const struct pawn *restrict pawn = battle->players[i].pawns[i];
		struct point location;

		if (!pawn->count) continue;

		location = commands[i].moves[commands[i].moves_count - 1].location;
		pawns_moved[location.y][location.x] = pawn;
	}

	for(i = 0; i < battle->players[i].pawns_count; ++i) // loop the pawns the player controls
	{
		const struct pawn *restrict pawn = battle->players[i].pawns[i];
		const struct pawn_command *restrict command = commands + i;

		if (!pawn->count) continue;

		if (command->action == PAWN_SHOOT)
		{
			// estimate shoot impact
		}
		else if ((command->action == PAWN_ASSAULT) && battlefield_neighbors(command->moves[command->moves_count - 1].location, command->target))
		{
			// estimate assault impact
		}
		else
		{
			// estimate fight impact

			struct pawn *victims[4];
			unsigned victims_count = pawn_victims(game, battle, pawn, command, victims);

			for(j = 0; j < victims_count; ++j)
				rating += damage_expected(pawn, (double)pawn->count / victims_count, victims[j]) * unit_importance(battle, victims[j]->troop->unit);
		}
	}
	for(i = 0; i < battle->pawns_count; ++i) // loop ally and enemy pawns
	{
		const struct pawn *restrict pawn = battle->pawns + i;
		//struct point location;

		if (!pawn->count) continue;
		if (pawn->troop->owner == player) continue;

		// location = pawn->moves[pawn->moves_count - 1].location;
		// battle->field[location.y][location.x]
	}

	/*
	if (allies(game, pawn->troop->owner, player))
		;
	else
		;
	// TODO walls close to the current position (if on the two diagonals, large bonus (gate blocking))
	*/

	return 1.0;
}

int computer_formation(const struct game *restrict game, struct battle *restrict battle, unsigned char player)
{
	//

	return 0;
}

int computer_battle(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	// TODO generate reachable for each pawn (is this necessary since it can change?)

	// TODO simulated annealing

	return 0;
}
