#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "combat.h"
#include "movement.h"
#include "computer.h"
#include "computer_battle.h"

#define MAP_COMMAND_PRIORITY_THRESHOLD 0.5 /* TODO maybe this should not be a macro */

#define RATING_DEFAULT 0.5

#define UNIT_IMPORTANCE_DEFAULT 10

#include <stdio.h>

struct pawn_command
{
	enum pawn_action action;
	struct point target;

	size_t moves_count;
	struct move moves[];
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

static unsigned battle_state_neighbors(const struct game *restrict game, const struct battle *restrict battle, struct pawn *restrict pawn, double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct point neighbors[static 4])
{
	unsigned speed = pawn->troop->unit->speed;
	struct point location = pawn->moves[pawn->moves_count - 1].location, target;

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

	return neighbors_count;
}

static void battle_state_set(struct pawn *restrict pawn, struct point neighbor, const struct game *restrict game, const struct battle *restrict battle, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
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

// TODO rewrite this
static double battle_state_rating(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	double rating = 0.0, rating_max = 0.0;

	size_t i, j;

	const struct pawn *pawns_moved[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH] = {0};

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

		// TODO current round should have more weight than the following rounds
		rating_max += 350.0 * 50.0;
		if (pawn->action == PAWN_SHOOT)
		{
			// estimate shoot impact
			// TODO this doesn't account for accuracy and damage spreading to nearby targets
			const struct pawn *victim = pawn->target.pawn;
			rating += unit_importance(victim->troop->unit) * damage_expected(pawn, pawn->count, victim);
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
				rating += unit_importance(victims[j]->troop->unit) * damage_expected(pawn, (double)pawn->count / victims_count, victims[j]);
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

			rating_max += 3 * 350.0 * 50.0;

			// TODO take into account obstacles on the way and damage splitting to neighboring fields
			if (attacker->ranged.damage && (round(battlefield_distance(pawn->moves[pawn->moves_count - 1].location, other->moves[0].location)) < attacker->ranged.range))
				rating += unit_importance(other->troop->unit) * damage_expected_ranged(pawn, pawn->count, other);

			status = path_distance(pawn->moves[pawn->moves_count - 1].location, other->moves[0].location, graph, obstacles, &distance);
			if (status < 0) ; // TODO memory error
			if (distance == INFINITY) continue; // TODO this information is available through reachable

			// TODO use a better formula here
			rating -= unit_importance(pawn->troop->unit) * damage_expected(other, other->count, pawn) * other->troop->unit->speed / distance;
			rating += unit_importance(other->troop->unit) * damage_expected(pawn, pawn->count, other) * pawn->troop->unit->speed / distance;

			// TODO this is supposed to make the computer prefer setting the action; is this the right way?
			if ((pawn->action == PAWN_FIGHT) && (pawn->target.pawn == other))
				rating += unit_importance(other->troop->unit) * damage_expected(pawn, pawn->count, other) * pawn->troop->unit->speed / distance;

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
			rating -= unit_importance(victims[j]->troop->unit) * damage_expected(pawn, (double)pawn->count / victims_count, victims[j]);
		}
	}

	// assert(rating_max);
	return rating / rating_max;
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
	double rating, rating_new;
	double temperature = 1.0;

	size_t i, j;

	size_t pawns_count = battle->players[player].pawns_count;
	struct pawn_command *backup;

	struct point neighbors[4];
	unsigned neighbors_count;

	int status = 0;

	struct pawn *restrict pawn;

	double (*reachable)[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];
	reachable = malloc(pawns_count * sizeof(*reachable));
	if (!reachable) return ERROR_MEMORY;
	backup = malloc(offsetof(struct pawn_command, moves) + 32 * sizeof(struct move)); // TODO change this 32
	if (!backup)
	{
		free(reachable);
		return ERROR_MEMORY;
	}

	// Cancel pawn actions and determine the reachable fields for each pawn.
	for(i = 0; i < pawns_count; ++i)
	{
		pawn = battle->players[player].pawns[i];

		pawn->action = 0;
		pawn->moves_count = 1;

		status = path_distances(pawn, graph, obstacles, reachable[i]);
		if (status < 0) goto finally;
	}

	// Choose suitable commands for the pawns of the player.
	rating = battle_state_rating(game, battle, player, graph, obstacles);
	printf(">>\n");
	for(step = 0; step < ANNEALING_STEPS; ++step)
	{
		unsigned try;

		for(try = 0; try < ANNEALING_TRIES; ++try)
		{
			printf(">> rating: %f\n", rating);

			i = random() % pawns_count;
			pawn = battle->players[player].pawns[i];

			neighbors_count = battle_state_neighbors(game, battle, pawn, reachable[i], neighbors);
			if (!neighbors_count) continue;

			// Remember current pawn command and set a new one.
			backup->action = pawn->action;
			if (pawn->action == PAWN_FIGHT) backup->target = pawn->target.pawn->moves[0].location;
			else backup->target = pawn->target.field;
			backup->moves_count = pawn->moves_count;
			memcpy(backup->moves, pawn->moves, pawn->moves_count * sizeof(*pawn->moves));
			battle_state_set(pawn, neighbors[random() % neighbors_count], game, battle, graph, obstacles);

			printf("%d,%d -> %d,%d\n", pawn->moves[0].location.x, pawn->moves[0].location.y, pawn->moves[pawn->moves_count - 1].location.x, pawn->moves[pawn->moves_count - 1].location.y);

			// Calculate the rating of the new set of commands.
			// Revert the new command if it is unacceptably worse than the current one.
			rating_new = battle_state_rating(game, battle, player, graph, obstacles);
			if (state_wanted(rating, rating_new, temperature)) rating = rating_new;
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

		temperature *= 0.95;
	}

	// Find the local maximum (best action) for each of the pawns.
	printf("find maximum >>\n");
	for(i = 0; i < pawns_count; ++i)
	{
		pawn = battle->players[player].pawns[i];

		neighbors_count = battle_state_neighbors(game, battle, pawn, reachable[i], neighbors);
		for(j = 0; j < neighbors_count; ++j)
		{
			printf(">> rating: %f\n", rating);

			// Remember current pawn command and set a new one.
			backup->action = pawn->action;
			if (pawn->action == PAWN_FIGHT) backup->target = pawn->target.pawn->moves[0].location;
			else backup->target = pawn->target.field;
			backup->moves_count = pawn->moves_count;
			memcpy(backup->moves, pawn->moves, pawn->moves_count * sizeof(*pawn->moves));
			battle_state_set(pawn, neighbors[j], game, battle, graph, obstacles);

			printf("%d,%d -> %d,%d\n", pawn->moves[0].location.x, pawn->moves[0].location.y, pawn->moves[pawn->moves_count - 1].location.x, pawn->moves[pawn->moves_count - 1].location.y);

			// Calculate the rating of the new set of commands.
			// Revert the new command if it is worse than the current one.
			rating_new = battle_state_rating(game, battle, player, graph, obstacles);
			if (state_wanted(rating, rating_new, temperature)) rating = rating_new;
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
	}

	printf("final rating: %f\n", rating);

finally:
	free(backup);
	free(reachable);

	return status;
}

unsigned calculate_battle(struct game *restrict game, struct region *restrict region)
{
	struct troop *restrict troop;

	size_t i;
	unsigned winner_alliance = 0;

	// Calculate the strength of each alliance participating in the battle.
	// TODO take siege and shooters into account
	double strength[PLAYERS_LIMIT] = {0};
	for(troop = region->troops; troop; troop = troop->_next)
		strength[game->players[troop->owner].alliance] += unit_importance(troop->unit) * troop->count;

	// Calculate total strength and find the strongest alliance.
	double strength_total = 0.0;
	unsigned alliances_count = 0;
	for(i = 1; i < PLAYERS_LIMIT; ++i)
	{
		if (!strength[i]) continue;

		strength_total += strength[i];
		alliances_count += 1;
		if (strength[i] > strength[winner_alliance])
			winner_alliance = i;
	}

	// Adjust troops count.
	// TODO use a real formula here (with some randomness)
	double count_factor = 1 - (strength_total - strength[winner_alliance]) / (alliances_count * strength[winner_alliance]);
	for(troop = region->troops; troop; troop = troop->_next)
	{
		if (game->players[troop->owner].alliance == winner_alliance)
			troop->count *= count_factor;
		else
			troop->count = 0;
	}

	return winner_alliance;
}
