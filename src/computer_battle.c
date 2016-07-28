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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "game.h"
#include "draw.h"
#include "map.h"
#include "pathfinding.h"
#include "movement.h"
#include "battle.h"
#include "combat.h"
#include "computer.h"
#include "computer_battle.h"

#define RATING_DEFAULT 0.5

#define UNIT_IMPORTANCE_DEFAULT 10

#include <stdio.h>

struct pawn_command
{
	struct array_moves path;
	enum pawn_action action;
    union
    {
        struct pawn *pawn;
        struct position position;
        struct battlefield *field;
    } target;
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

static unsigned battle_state_neighbors(const struct game *restrict game, const struct battle *restrict battle, struct pawn *restrict pawn, double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct tile neighbors[static 4])
{
	// TODO exclude positions that are too far to get to in one round

	unsigned speed = pawn->troop->unit->speed;
	struct position position = *pawn->path.data;
	struct tile target;

	unsigned neighbors_count = 0;

	target = (struct tile){(unsigned)position.x - 1, (unsigned)position.y};
	if (in_battlefield(target.x, target.y) && (speed >= reachable[target.y][target.x]) && battlefield_passable(game, &battle->field[target.y][target.x], pawn->troop->owner))
		neighbors[neighbors_count++] = target;

	target = (struct tile){(unsigned)position.x + 1, (unsigned)position.y};
	if (in_battlefield(target.x, target.y) && (speed >= reachable[target.y][target.x]) && battlefield_passable(game, &battle->field[target.y][target.x], pawn->troop->owner))
		neighbors[neighbors_count++] = target;

	target = (struct tile){(unsigned)position.x, (unsigned)position.y - 1};
	if (in_battlefield(target.x, target.y) && (speed >= reachable[target.y][target.x]) && battlefield_passable(game, &battle->field[target.y][target.x], pawn->troop->owner))
		neighbors[neighbors_count++] = target;

	target = (struct tile){(unsigned)position.x, (unsigned)position.y + 1};
	if (in_battlefield(target.x, target.y) && (speed >= reachable[target.y][target.x]) && battlefield_passable(game, &battle->field[target.y][target.x], pawn->troop->owner))
		neighbors[neighbors_count++] = target;

	// TODO support assault

	return neighbors_count;
}

static void battle_state_set(struct pawn *restrict pawn, struct tile neighbor, const struct game *restrict game, struct battle *restrict battle, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	struct battlefield *restrict field = battle_field(battle, neighbor);

	if (*field->pawns)
	{
		// TODO what if there is more than one pawn at the field

		if (combat_order_shoot(game, battle, obstacles, pawn, field->pawns[0]->position))
			return;
		else if (combat_order_fight(game, battle, obstacles, pawn, *field->pawns))
			return;
	}
	else if (field->blockage == BLOCKAGE_OBSTACLE)
	{
		if (combat_order_assault(game, pawn, field))
			return;
	}

	pawn->path.count = 0;
	movement_queue(pawn, (struct position){neighbor.x, neighbor.y}, graph, obstacles);
}

// TODO is 8 enough for victims?
static unsigned victims_fight_find(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict fighter, const struct position *restrict positions, const struct pawn *restrict victims[static 8])
{
	unsigned char fighter_alliance = game->players[fighter->troop->owner].alliance;
	struct position fighter_position = positions[fighter - battle->pawns];
	unsigned victims_count = 0;

	// TODO what if one of the pawns is on a tower

	// If the pawn has a fight target and is close enough to it, set that pawn as a victim.
	if (fighter->action == ACTION_FIGHT)
	{
		struct position target_position = positions[fighter->target.pawn - battle->pawns];
		if (battlefield_distance(fighter_position, target_position) <= DISTANCE_MELEE)
		{
			victims[victims_count++] = fighter->target.pawn;
			return victims_count;
		}
	}

	// Set all enemy pawns close enough as victims.
	for(size_t i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *victim = battle->pawns + i;
		if (!victim->count)
			continue;
		if (game->players[victim->troop->owner].alliance == fighter_alliance)
			continue;
		if (battlefield_distance(fighter_position, positions[i]) <= DISTANCE_MELEE)
			victims[victims_count++] = victim;
	}

	// assert(victims_count <= 8); // TODO make sure this is true
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
// TODO can I make this update the rating after a change (instead of recalculating everything)
static double battle_state_rating(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	return 0.5;

	double rating = 0.0, rating_max = 0.0;

	size_t i, j;

	const struct pawn *victims[8]; // TODO is this big enough?
	unsigned victims_count;

	struct position *positions = malloc(battle->pawns_count * sizeof(*positions));
	if (!positions) return NAN;

	// TODO optimize this

	// WARNING: We assume the locations, where the pawns are commanded to go, will not be occupied. // TODO why?

	// Estimate which pawn will occupy a given location after the move.
	for(i = 0; i < battle->pawns_count; ++i)
	{
		const struct pawn *restrict pawn = battle->pawns + i;

		if (!pawn->count) continue;

		if (pawn->troop->owner == player)
		{
			switch (pawn->action)
			{
			case ACTION_FIGHT:
				positions[i] = pawn->target.pawn->position;
				break;

			case ACTION_ASSAULT:
				positions[i] = (struct position){pawn->target.field->tile.x, pawn->target.field->tile.y};
				break;

			default:
				positions[i] = pawn->path.count ? *pawn->path.data : pawn->position;
				break;
			}
		}
		else positions[i] = pawn->position; // TODO do a better estimation here
	}

	// TODO don't run away from ranged units unless you're faster?

	// TODO take ally pawns into account for: benefits and dangers for the following turns; damage effects from enemies

	// Estimate how beneficial is the command given to each of the player's pawns.
	for(i = 0; i < battle->players[player].pawns_count; ++i) // loop the pawns the player controls
	{
		const struct pawn *restrict pawn = battle->players[player].pawns[i];

		if (!pawn->count) continue;

		// TODO max rating should be related to max damage

		// TODO current round should have more weight than the following rounds

		rating_max += 350.0 * 50.0;
		if (pawn->action == ACTION_SHOOT)
		{
			// TODO this doesn't account for accuracy and damage spreading to nearby targets

			const struct pawn *victim;
			struct tile tile = (struct tile){(size_t)(pawn->target.position.x + 0.5), (size_t)(pawn->target.position.y + 0.5)};
			const struct battlefield *restrict field = battle_field(battle, tile);

			for(j = 0; victim = field->pawns[j]; ++j)
				if (position_eq(pawn->target.position, victim->position))
					goto found;
			// assert(0);
found:

			// estimate shoot impact
			rating += unit_importance(victim->troop->unit, 0) * damage_expected_ranged(pawn, pawn->count, victim);
		}
		else if (pawn->action == ACTION_ASSAULT) // TODO remove the comment && can_assault(*pawn->path.data, pawn->target.field))
		{
			const struct battlefield *restrict field = pawn->target.field;

			// estimate assault impact
			rating += 30.0 * damage_expected_assault(pawn, pawn->count, field);
		}
		else
		{
			// estimate fight impact
			victims_count = victims_fight_find(game, battle, pawn, positions, victims);
			for(j = 0; j < victims_count; ++j)
				rating += unit_importance(victims[j]->troop->unit, 0) * damage_expected(pawn, (double)pawn->count / victims_count, victims[j]);
		}

		// Estimate benefits and dangers for the following turns (moving, fighting, shooting, assault).
		for(j = 0; j < battle->pawns_count; ++j) // loop through enemy pawns
		{
			const struct unit *restrict attacker = pawn->troop->unit;
			const struct pawn *restrict other = battle->pawns + j;
			double distance;

			// TODO finish this

			if (!pawn->count) continue;
			if (allies(game, other->troop->owner, player)) continue;

			rating_max += 3 * 350.0 * 50.0;

			distance = battlefield_distance(positions[i], positions[j]);

			// TODO take into account obstacles on the way and damage splitting to neighboring fields
			if (attacker->ranged.weapon && (distance <= attacker->ranged.range))
				rating += unit_importance(other->troop->unit, 0) * damage_expected_ranged(pawn, pawn->count, other);

			// TODO check if other is reachable
			// TODO use a better formula here
			rating -= unit_importance(pawn->troop->unit, 0) * damage_expected(other, other->count, pawn) * other->troop->unit->speed / distance;
			rating += unit_importance(other->troop->unit, 0) * damage_expected(pawn, pawn->count, other) * pawn->troop->unit->speed / distance;
		}

		if (pawn->action == ACTION_SHOOT)
		{
			// TODO this is supposed to make the computer prefer shooting; is this the right way?
			double distance = battlefield_distance(positions[i], positions[pawn->target.pawn - battle->pawns]);
			rating += 2 * unit_importance(pawn->target.pawn->troop->unit, 0) * damage_expected_ranged(pawn, pawn->count, pawn->target.pawn) * pawn->troop->unit->speed / distance;
		}
		else if (pawn->action == ACTION_FIGHT)
		{
			// TODO this is supposed to make the computer prefer setting the action; is this the right way?
			double distance = battlefield_distance(positions[i], positions[pawn->target.pawn - battle->pawns]);
			rating += unit_importance(pawn->target.pawn->troop->unit, 0) * damage_expected(pawn, pawn->count, pawn->target.pawn) * pawn->troop->unit->speed / distance;
		}

		// TODO assault evaluation

		// TODO ally walls close to the current position (if on the two diagonals, large bonus (gate blocking))
	}

	// Estimate how bad will the damage effects from enemies be.
	for(i = 0; i < battle->pawns_count; ++i) // loop enemy pawns
	{
		const struct pawn *restrict pawn = battle->pawns + i;

		if (!pawn->count) continue;
		if (allies(game, pawn->troop->owner, player)) continue;

		// TODO guess when a pawn will prefer shoot/assault or fighting specific target

		victims_count = victims_fight_find(game, battle, pawn, positions, victims);
		for(j = 0; j < victims_count; ++j)
		{
			// TODO subtracting from rating is probably a bad idea

			if (victims[j]->troop->owner != player) continue; // skip pawns owned by other players
			rating -= unit_importance(victims[j]->troop->unit, 0) * damage_expected(pawn, (double)pawn->count / victims_count, victims[j]);
		}
	}

	free(positions);

	// assert(rating_max);
	return rating / rating_max;
}

int computer_formation(const struct game *restrict game, struct battle *restrict battle, unsigned char player)
{
	// TODO
	return 0;
}

static int command_remember(struct pawn_command *restrict command, struct pawn *restrict pawn)
{
	if (pawn->path.count)
	{
		if ((command->path.capacity < pawn->path.count) && (array_moves_expand(&command->path, pawn->path.count) < 0))
			return ERROR_MEMORY;

		memcpy(command->path.data, pawn->path.data, pawn->path.count);
	}
	command->path.count = pawn->path.count;

	command->action = pawn->action;
	switch (pawn->action)
	{
	case ACTION_FIGHT:
		command->target.pawn = pawn->target.pawn;
		break;

	case ACTION_SHOOT:
		command->target.position = pawn->target.position;
		break;

	case ACTION_ASSAULT:
		command->target.field = pawn->target.field;
		break;
	}

	return 0;
}

static void command_restore(struct pawn *restrict pawn, struct pawn_command *restrict command)
{
	if (command->path.count)
		memcpy(pawn->path.data, command->path.data, command->path.count);
	pawn->path.count = command->path.count;

	pawn->action = command->action;
	switch (command->action)
	{
	case ACTION_FIGHT:
		pawn->target.pawn = command->target.pawn;
		break;

	case ACTION_SHOOT:
		pawn->target.position = command->target.position;
		break;

	case ACTION_ASSAULT:
		pawn->target.field = command->target.field;
		break;
	}
}

// Determine the behavior of the computer using simulated annealing.
int computer_battle(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	unsigned step;
	double rating, rating_new;
	double temperature = 1.0;

	double (*reachable)[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];

	size_t pawns_count = battle->players[player].pawns_count;
	struct pawn_command backup;

	struct pawn *restrict pawn;

	struct tile neighbors[4];
	unsigned neighbors_count;

	size_t i, j;
	int status;

	reachable = malloc(pawns_count * sizeof(*reachable));
	if (!reachable) return ERROR_MEMORY;

	for(i = 0; i < pawns_count; ++i)
	{
		pawn = battle->players[player].pawns[i];

		// Cancel pawn command.
		pawn->path.count = 0;
		pawn->action = 0;

		// Determine which fields are reachable by the pawn.
		status = path_distances(pawn, graph, obstacles, reachable[i]);
		if (status < 0) goto finally;
	}

	// Choose suitable commands for the pawns of the player.
	rating = battle_state_rating(game, battle, player, graph, obstacles);
	for(step = 0; step < ANNEALING_STEPS; ++step)
	{
		unsigned try;

		for(try = 0; try < ANNEALING_TRIES; ++try)
		{
			i = random() % pawns_count;
			pawn = battle->players[player].pawns[i];

			neighbors_count = battle_state_neighbors(game, battle, pawn, reachable[i], neighbors);
			if (!neighbors_count) continue;

			// Remember current pawn command and set a new one.
			status = command_remember(&backup, pawn);
			if (status < 0) goto finally;
			battle_state_set(pawn, neighbors[random() % neighbors_count], game, battle, graph, obstacles);

			// Calculate the rating of the new set of commands.
			// Restore the original command if the new one is unacceptably worse.
			rating_new = battle_state_rating(game, battle, player, graph, obstacles);
			if (state_wanted(rating, rating_new, temperature)) rating = rating_new;
			else command_restore(pawn, &backup);
		}

		temperature *= 0.95;
	}

	// Find the local maximum (best action) for each of the pawns.
	for(i = 0; i < pawns_count; ++i)
	{
		pawn = battle->players[player].pawns[i];

search:
		neighbors_count = battle_state_neighbors(game, battle, pawn, reachable[i], neighbors);
		for(j = 0; j < neighbors_count; ++j)
		{
			// Remember current pawn command and set a new one.
			status = command_remember(&backup, pawn);
			if (status < 0) goto finally;
			battle_state_set(pawn, neighbors[j], game, battle, graph, obstacles);

			// Calculate the rating of the new set of commands.
			// Restore the original command if the new one is unacceptably worse.
			rating_new = battle_state_rating(game, battle, player, graph, obstacles);
			if (state_wanted(rating, rating_new, temperature))
			{
				rating = rating_new;
				goto search; // state changed; search for neighbors of the new state
			}
			else command_restore(pawn, &backup);
		}
	}

	status = 0;

finally:
	array_moves_term(&backup.path);
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
		strength[game->players[troop->owner].alliance] += unit_importance(troop->unit, 0) * troop->count;

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
