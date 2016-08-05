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

#include <assert.h>
#include <math.h>
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

#define FIGHT_ERROR 0.8 /* multiplier that adjustes for the chance of pawns being too far to fight */

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

static unsigned battle_state_neighbors(const struct game *restrict game, const struct battle *restrict battle, struct pawn *restrict pawn, double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct tile neighbors[static 8])
{
	struct position position = (pawn->path.count ? *pawn->path.data : pawn->position);
	struct tile target;
	double distance_limit;

	unsigned neighbors_count = 0;

	distance_limit = pawn->troop->unit->speed + DISTANCE_MELEE;
	if (pawn->troop->unit->ranged.range > distance_limit)
		distance_limit = pawn->troop->unit->ranged.range;

	// TODO some of the neighbors may be unreachable (due to blockages)

	target = (struct tile){(unsigned)(position.x + 1), (unsigned)position.y};
	if (in_battlefield(target.x, target.y) && (reachable[target.y][target.x] <= distance_limit))
		neighbors[neighbors_count++] = target;

	target = (struct tile){(unsigned)(position.x + M_SQRT2 / 2), (unsigned)(position.y + M_SQRT2 / 2)};
	if (in_battlefield(target.x, target.y) && (reachable[target.y][target.x] <= distance_limit))
		neighbors[neighbors_count++] = target;

	target = (struct tile){(unsigned)position.x, (unsigned)(position.y - 1)};
	if (in_battlefield(target.x, target.y) && (reachable[target.y][target.x] <= distance_limit))
		neighbors[neighbors_count++] = target;

	target = (struct tile){(unsigned)(position.x - M_SQRT2 / 2), (unsigned)(position.y - M_SQRT2 / 2)};
	if (in_battlefield(target.x, target.y) && (reachable[target.y][target.x] <= distance_limit))
		neighbors[neighbors_count++] = target;

	target = (struct tile){(unsigned)(position.x - 1), (unsigned)position.y};
	if (in_battlefield(target.x, target.y) && (reachable[target.y][target.x] <= distance_limit))
		neighbors[neighbors_count++] = target;

	target = (struct tile){(unsigned)(position.x - M_SQRT2 / 2), (unsigned)(position.y + M_SQRT2 / 2)};
	if (in_battlefield(target.x, target.y) && (reachable[target.y][target.x] <= distance_limit))
		neighbors[neighbors_count++] = target;

	target = (struct tile){(unsigned)position.x, (unsigned)(position.y + 1)};
	if (in_battlefield(target.x, target.y) && (reachable[target.y][target.x] <= distance_limit))
		neighbors[neighbors_count++] = target;

	target = (struct tile){(unsigned)(position.x + M_SQRT2 / 2), (unsigned)(position.y + M_SQRT2 / 2)};
	if (in_battlefield(target.x, target.y) && (reachable[target.y][target.x] <= distance_limit))
		neighbors[neighbors_count++] = target;

	return neighbors_count;
}

static void battle_state_set(struct pawn *restrict pawn, struct tile neighbor, const struct game *restrict game, struct battle *restrict battle, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	struct battlefield *restrict field = battle_field(battle, neighbor);
	struct position destination = {neighbor.x, neighbor.y};

	if (*field->pawns && !allies(game, field->pawns[0]->troop->owner, pawn->troop->owner))
	{
		// TODO what if there is more than one pawn at the field
		// TODO shooters are biased - they prefer shooting over fighting; they should choose the better option instead

		if (combat_order_shoot(game, battle, obstacles, pawn, field->pawns[0]->position))
			return;
		else if (combat_order_shoot(game, battle, obstacles, pawn, destination))
			return;
		else if (battlefield_distance(pawn->position, field->pawns[0]->position) <= pawn->troop->unit->speed + DISTANCE_MELEE)
		{
			if (combat_order_fight(game, battle, obstacles, pawn, *field->pawns))
				return;
		}
	}
	else if (field->blockage == BLOCKAGE_OBSTACLE)
	{
		if (battlefield_distance(pawn->position, destination) <= pawn->troop->unit->speed + DISTANCE_MELEE)
		{
			if (combat_order_assault(game, pawn, field))
				return;
		}
	}

	// TODO what should I do when I'm unable to set the state

	pawn->action = 0;
	pawn->path.count = 0;
	if (!position_eq(pawn->position, destination))
		if (movement_queue(pawn, destination, graph, obstacles) < 0)
			return; // unable to set state

	pawn->path.data[0] = movement_position(pawn);
	pawn->moves.count = 0;
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

	assert(victims_count <= 8);
	return victims_count;
}

static double attack_rating(double damage_expected, unsigned victim_count, const struct unit *restrict victim_unit, const struct garrison_info *restrict info)
{
	double deaths = damage_expected / victim_unit->health;
	if (deaths > victim_count)
		deaths = victim_count;
	return unit_importance(victim_unit, info) * deaths;
}

// TODO improve this
// TODO optimize this
static double battle_state_rating(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	size_t i, j;

	struct position *positions;
	const struct pawn *victims[8]; // TODO is this big enough?
	unsigned victims_count;
	const struct garrison_info *info;

	double rating = 0.0, rating_max = 0.0;

	positions = malloc(battle->pawns_count * sizeof(*positions));
	if (!positions) return NAN;

	info = garrison_info(battle->region);

	// TODO think whether subtracting from rating is a good idea

	// Predict what position will each pawn occupy after the move.
	// WARNING: Assume the posisions, where the pawns are commanded to go, will not be occupied.
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
		struct position pawn_position;
		double distance, distance_min;
		double attack_impact;

		if (!pawn->count) continue;

		pawn_position = positions[pawn - battle->pawns];

		if (pawn->action == ACTION_SHOOT)
		{
			// TODO this doesn't account for accuracy and damage spreading to nearby targets

			const struct pawn *victim;
			struct tile tile = (struct tile){(size_t)pawn->target.position.x, (size_t)pawn->target.position.y};
			const struct battlefield *restrict field = battle_field(battle, tile);

			for(j = 0; victim = field->pawns[j]; ++j)
				if (position_eq(pawn->target.position, victim->position))
					goto found;
			assert(0);
found:
			// estimate shoot impact
			rating += attack_rating(damage_expected_ranged(pawn, pawn->count, victim), victim->count, victim->troop->unit, info);
			printf("shoot impact %f (%f,%f)\n", attack_rating(damage_expected_ranged(pawn, pawn->count, victim), victim->count, victim->troop->unit, info), pawn->position.x, pawn->position.y);
		}
		else if (pawn->action == ACTION_ASSAULT) // TODO remove the comment && can_assault(*pawn->path.data, pawn->target.field))
		{
			const struct battlefield *restrict field = pawn->target.field;

			// estimate assault impact
			rating += attack_rating(damage_expected_assault(pawn, pawn->count, field), 1, field->unit, info);
		}
		else
		{
			// estimate fight impact
			double fight_rating = 0;
			victims_count = victims_fight_find(game, battle, pawn, positions, victims);
			for(j = 0; j < victims_count; ++j)
			{
				fight_rating += attack_rating(damage_expected(pawn, (double)pawn->count / victims_count, victims[j]), victims[j]->count, victims[j]->troop->unit, info);
				printf("fight impact %f (%f,%f)\n", attack_rating(damage_expected(pawn, (double)pawn->count / victims_count, victims[j]), victims[j]->count, victims[j]->troop->unit, info), pawn->position.x, pawn->position.y);
			}
			if ((pawn->action != ACTION_FIGHT) || (pawn->target.pawn != victims[0]))
				fight_rating *= FIGHT_ERROR;
			rating += fight_rating;
			// TODO increase impact when a fight target is set
		}

		// Estimate benefits and dangers for the following turns (moving, fighting, shooting, assault).
		for(j = 0; j < battle->pawns_count; ++j) // loop through enemy pawns
		{
			const struct pawn *restrict other = battle->pawns + j;
			unsigned other_count;

			if (!other->count) continue;
			if (allies(game, other->troop->owner, player)) continue;

			other_count = other->count;

			distance = battlefield_distance(pawn_position, positions[j]);
			distance_min = battlefield_distance(pawn->position, positions[j]) - pawn->troop->unit->speed;
			if (distance_min < 0) distance_min = 0;

			// TODO below add the rating, divided by the number of rounds necessary to reach the victim + 1 (as a double)
			// TODO check if the pawn will be able to shoot; take into account obstacles on the way and damage splitting to neighboring fields
			// TODO check if the pawn is really reachable for melee fight (no obstacles)

			// Add rating for future possibility of attacking.
			if (pawn->troop->unit->ranged.weapon)
			{
				double damage = damage_expected_ranged(pawn, pawn->count, other);
				unsigned deaths = (unsigned)(damage / other->troop->unit->health);

				attack_impact = attack_rating(damage, other_count, other->troop->unit, info);
				rating += attack_impact / (distance / pawn->troop->unit->ranged.range + 1);
				printf("shoot closeness impact %f (%f,%f)\n", attack_impact / (distance / pawn->troop->unit->ranged.range + 1), other->position.x, other->position.y);
				rating_max += attack_impact + attack_impact / (distance_min / pawn->troop->unit->ranged.range + 1);

				other_count = ((other_count >= deaths) ? other_count - deaths : 0); // take into account the enemy troops that will die from the shooting
			}
			attack_impact = attack_rating(damage_expected(pawn, pawn->count, other), other_count, other->troop->unit, info);
			if (!pawn->troop->unit->ranged.weapon)
			{
				rating += attack_impact / (distance / pawn->troop->unit->speed + 1);
				printf("closeness impact %f (%f,%f)\n", attack_impact / (distance / pawn->troop->unit->speed + 1), other->position.x, other->position.y);
			}
			rating_max += attack_impact + attack_impact / (distance_min / pawn->troop->unit->speed + 1);

			// Subtract rating for future possibility of being attacked.
			if (other->troop->unit->ranged.weapon)
				rating -= attack_rating(damage_expected_ranged(other, other_count, pawn), pawn->count, pawn->troop->unit, info) / (distance / other->troop->unit->ranged.range + 1);
			else
			{
				rating -= attack_rating(damage_expected(other, other_count, pawn), pawn->count, pawn->troop->unit, info) / (distance / other->troop->unit->speed + 1);
				printf("enemy closeness impact -%f (%f,%f)\n", attack_rating(damage_expected(other, other_count, pawn), pawn->count, pawn->troop->unit, info) / (distance / other->troop->unit->speed + 1), other->position.x, other->position.y);
			}
		}

		// Add rating for future possibility of assaulting.
		// TODO do this only for siege machines (make a function that checks whether a unit is a siege machine)
		for(size_t y = 0; y < BATTLEFIELD_HEIGHT; ++y)
			for(size_t x = 0; x < BATTLEFIELD_WIDTH; ++x)
			{
				const struct battlefield *restrict field = &battle->field[y][x];
				struct position field_position;

				if (field->blockage != BLOCKAGE_OBSTACLE)
					continue;

				field_position = (struct position){x + 0.5, y + 0.5};
				distance = battlefield_distance(pawn_position, field_position);
				distance_min = battlefield_distance(pawn->position, field_position) - pawn->troop->unit->speed;
				if (distance_min < 0) distance_min = 0;

				attack_impact = attack_rating(damage_expected_assault(pawn, pawn->count, field), 1, field->unit, info);
				rating += attack_impact / (distance / pawn->troop->unit->speed + 1);

				rating_max += attack_impact / (distance_min / pawn->troop->unit->speed + 1);

				// TODO ally walls close to the current position (if on the two diagonals, large bonus (gate blocking))
			}
	}

	// Estimate how bad will the damage effects from enemies be.
	for(i = 0; i < battle->pawns_count; ++i) // loop enemy pawns
	{
		const struct pawn *restrict pawn = battle->pawns + i;
		double fight_rating;

		if (!pawn->count) continue;
		if (allies(game, pawn->troop->owner, player)) continue;

		// TODO guess when a pawn will prefer shoot/assault or fighting specific target

		// Adjust rating for damage from fighting.
		fight_rating = 0;
		victims_count = victims_fight_find(game, battle, pawn, positions, victims);
		for(j = 0; j < victims_count; ++j)
		{
			if (victims[j]->troop->owner != player)
				continue; // skip pawns owned by other players

			fight_rating -= attack_rating(damage_expected(pawn, (double)pawn->count / victims_count, victims[j]), victims[j]->count, victims[j]->troop->unit, info);
		}
		printf("enemy fight impact %f (%f,%f)\n", fight_rating, pawn->position.x, pawn->position.y);
		rating += fight_rating;

		// Adjust rating for damage from shooting.
		// TODO check if the pawn will be able to shoot (will there be enemies in fighting distance)
		if (pawn->troop->unit->ranged.weapon)
			rating -= attack_rating(damage_expected_ranged(pawn, pawn->count, victims[j]), victims[j]->count, victims[j]->troop->unit, info);
	}

	if (battle->pawns[1].path.count)
		printf("%f (%f,%f)\n", rating / rating_max, battle->pawns[1].path.data[0].x, battle->pawns[1].path.data[0].y);
	else
		printf("%f (%f,%f)\n", rating / rating_max, battle->pawns[1].position.x, battle->pawns[1].position.y);

	free(positions);

	assert(rating_max);
	return rating / rating_max;
}

int computer_formation(const struct game *restrict game, struct battle *restrict battle, unsigned char player)
{
	// TODO implement this
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

	struct tile neighbors[8];
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
		for(unsigned try = 0; try < ANNEALING_TRIES; ++try)
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
			if (state_wanted(rating, rating_new, temperature))
			{
				rating = rating_new;
				printf("rating=%f\n", rating);
			}
			else command_restore(pawn, &backup);
		}

		temperature *= ANNEALING_COOLDOWN;
	}

	// Find the local maximum (best action) for each of the pawns.
	printf("GOING for local maximum: %f\n", rating);
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
			if (rating_new > rating)
			{
				rating = rating_new;
				printf("rating=%f\n", rating);
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
	unsigned winner_alliance = 0;

	// TODO make this work better for assault

	// Calculate the strength of each alliance participating in the battle.
	// TODO take siege and shooters into account
	double strength[PLAYERS_LIMIT] = {0};
	for(troop = region->troops; troop; troop = troop->_next)
		strength[game->players[troop->owner].alliance] += unit_importance(troop->unit, 0) * troop->count;

	// Calculate total strength and find the strongest alliance.
	double strength_total = 0.0;
	unsigned alliances_count = 0;
	for(size_t i = 1; i < PLAYERS_LIMIT; ++i)
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
