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

struct pawn_distance
{
	struct pawn *pawn;
	double distance;
};

#define heap_name heap_pawn_distance
#define heap_type struct pawn_distance
#define heap_above(a, b) ((a).distance >= (b).distance)
#include "generic/heap.g"

#define FIGHT_ERROR 0.8 /* multiplier that adjustes for the chance of pawns being too far to fight */

#define NEIGHBOR_STATES_LIMIT 10

struct pawn_command
{
	struct
	{
		size_t count;
		struct position data[PATH_QUEUE_LIMIT];
	} path;
	enum pawn_action action;
    union
    {
        struct pawn *pawn;
        struct position position;
        struct battlefield *field;
    } target;
	struct position position;
};

#include <stdio.h>

static void distance_sort(struct heap_pawn_distance *closeness)
{
	size_t pawns_count = closeness->count;
	heap_pawn_distance_heapify(closeness);
	while (closeness->count)
	{
		struct pawn_distance pawn_distance = closeness->data[0];
		heap_pawn_distance_pop(closeness);
		closeness->data[closeness->count] = pawn_distance;
	}
	closeness->count = pawns_count;
}

// Returns the number of pawns that are no farther than distance.
static size_t distance_search(const struct heap_pawn_distance *restrict closeness, double distance)
{
	size_t left = 0, right = closeness->count;

	while (left < right)
	{
		size_t index = (right - left) / 2 + left;
		if (closeness->data[index].distance <= distance)
			left = index + 1;
		else
			right = index;
	}

	return left;
}

static struct heap_pawn_distance *closeness_index(const struct battle *restrict battle)
{
	struct heap_pawn_distance *closeness;

	unsigned char *buffer = malloc(battle->pawns_count * (sizeof(*closeness) + (battle->pawns_count - 1) * sizeof(*closeness->data)));
	if (!buffer) return 0;

	// For each pawn, initalize a list of the other pawns ordered by distance ascending.
	closeness = (void *)buffer;
	buffer += battle->pawns_count * sizeof(*closeness);
	for(size_t i = 0; i < battle->pawns_count; ++i)
	{
		if (!battle->pawns[i].count)
			continue;

		closeness[i].count = 0;
		closeness[i].data = (void *)buffer;
		buffer += (battle->pawns_count - 1) * sizeof(*closeness->data);

		for(size_t j = 0; j < battle->pawns_count; ++j)
		{
			if (!battle->pawns[j].count)
				continue;
			if (j == i)
				continue;
			closeness[i].data[closeness[i].count].pawn = battle->pawns + j;
			closeness[i].data[closeness[i].count].distance = battlefield_distance(battle->pawns[i].position, battle->pawns[j].position);
			closeness[i].count += 1;
		}

		// Sort the pawns by distance.
		distance_sort(closeness + i);
	}

	return closeness;
}

static void closeness_index_update(struct heap_pawn_distance *restrict closeness, const struct battle *restrict battle, const struct position *restrict positions, size_t index)
{
	size_t i, j;
	for(i = 0; i < battle->pawns_count; ++i)
	{
		double distance;
		struct pawn_distance swap;
		size_t position;

		if (!battle->pawns[i].count)
			continue;

		if (i == index)
		{
			// Update distance to each of the pawns.
			for(j = 0; j < closeness[i].count; ++j)
			{
				size_t index_current = closeness[i].data[j].pawn - battle->pawns;
				closeness[i].data[j].distance = battlefield_distance(positions[i], positions[index_current]);
			}

			distance_sort(closeness + i);

			continue;
		}

		distance = battlefield_distance(positions[i], positions[index]);
		for(j = 0; j < closeness[i].count; ++j)
		{
			size_t index_current = closeness[i].data[j].pawn - battle->pawns;
			if (index_current == index)
				break;
		}
		assert(j < closeness[i].count);

		position = j;
		swap = closeness[i].data[position];
		if (distance < swap.distance) // the pawn moved closer
		{
			// Determine which elements need to be shifted right.
			while (j && (distance < closeness[i].data[j - 1].distance))
				j -= 1;
			if (j < position)
				memmove(closeness[i].data + j + 1, closeness[i].data + j, (position - j) * sizeof(*closeness[i].data));
		}
		else
		{
			// Determine which elements need to be shifted left.
			while (((j + 1) < closeness[i].count) && (distance > closeness[i].data[j + 1].distance))
				j += 1;
			if (j > position)
				memmove(closeness[i].data + position, closeness[i].data + position + 1, (j - position) * sizeof(*closeness[i].data));
		}
		swap.distance = distance;
		closeness[i].data[j] = swap;
	}
}

static unsigned neighbors_shoot(const struct game *restrict game, const struct pawn *restrict pawn, const struct heap_pawn_distance *restrict closeness, struct pawn_command neighbors[static NEIGHBOR_STATES_LIMIT - 1])
{
	unsigned neighbors_count = 0;

	// TODO what if the target is too far due to a wall

	size_t targets_count = distance_search(closeness, pawn->troop->unit->ranged.range);
	for(size_t i = 0; i < targets_count; ++i)
	{
		const struct pawn *restrict target = closeness->data[i].pawn;
		if ((pawn->action == ACTION_SHOOT) && position_eq(pawn->target.position, target->position))
			continue; // don't add current state as neighbor

		if (!allies(game, pawn->troop->owner, target->troop->owner))
		{
			neighbors[neighbors_count].action = ACTION_SHOOT;
			neighbors[neighbors_count].target.position = target->position;
			neighbors[neighbors_count].position = pawn->position;

			neighbors_count += 1;
			if (neighbors_count == NEIGHBOR_STATES_LIMIT - 1)
				break;
		}
	}

	return neighbors_count;
}

static unsigned neighbors_static(struct position position, enum pawn_action action, struct pawn_command neighbors[static 1])
{
	neighbors->action = action;
	neighbors->target.position = position;
	neighbors->position = position;
	return 1;
}

static unsigned neighbors_move(struct battle *restrict battle, const struct pawn *restrict pawn, double x, double y, const struct heap_pawn_distance *restrict closeness, double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct pawn_command neighbors[static 1])
{
	struct tile tile = (struct tile){(unsigned)x, (unsigned)y};
	struct battlefield *restrict field;

	if (!in_battlefield(x, y))
		return 0;
	if (pawn->troop->unit->speed + 1 < reachable[tile.y][tile.x])
		return 0;

	field = &battle->field[tile.y][tile.x];
	switch (field->blockage)
	{
	case BLOCKAGE_WALL:
	case BLOCKAGE_GATE:
		neighbors->action = ACTION_ASSAULT;
		neighbors->target.field = field;
		neighbors->position = (struct position){x, y};
		return 1;

	case BLOCKAGE_NONE:
		// Don't add move if there is already a pawn at that position.
		if (closeness->count && (closeness->data[0].distance < 2 * PAWN_RADIUS))
			return 0;

		neighbors->action = ACTION_HOLD;
		neighbors->position = (struct position){x, y};
		return 1;

	default:
		return 0;
	}
}

static unsigned neighbors_fight(const struct game *restrict game, const struct pawn *restrict pawn, struct position position, const struct heap_pawn_distance *restrict closeness, struct pawn_command *neighbors, size_t neighbors_limit)
{
	size_t neighbors_count = 0;

	for(size_t i = 0; i < closeness->count; ++i)
	{
		if (closeness->data[i].distance > DISTANCE_MELEE)
			break;
		if (!allies(game, pawn->troop->owner, closeness->data[i].pawn->troop->owner))
		{
			neighbors->action = ACTION_FIGHT;
			neighbors->target.pawn = closeness->data[i].pawn;
			neighbors->position = position;

			neighbors_count += 1;
			if (neighbors_count == neighbors_limit)
				break;
		}
	}

	return neighbors_count;
}

static unsigned battle_state_neighbors(const struct game *restrict game, struct battle *restrict battle, struct pawn *restrict pawn, double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct pawn_command neighbors[static NEIGHBOR_STATES_LIMIT], struct heap_pawn_distance *restrict closeness)
{
	struct position position = (pawn->path.count ? *pawn->path.data : pawn->position);

	// WARNING: path is not set by this function

	unsigned neighbors_count = 0;

	size_t i;

	if (!pawn->path.count)
		switch (pawn->action)
		{
		case ACTION_SHOOT:
			neighbors_count += neighbors_shoot(game, pawn, closeness, neighbors + neighbors_count);
			neighbors_count += neighbors_static(position, ACTION_GUARD, neighbors + neighbors_count);
			return neighbors_count;

		case ACTION_GUARD:
			neighbors_count += neighbors_static(position, ACTION_HOLD, neighbors + neighbors_count);

			// If no melee pawns are stopping the pawn from shooting, add shoot actions.
			i = 0;
			do
			{
				if ((i == closeness->count) || (closeness->data[i].distance > DISTANCE_MELEE))
				{
					neighbors_count += neighbors_shoot(game, pawn, closeness, neighbors + neighbors_count);
					break;
				}
			} while (allies(game, pawn->troop->owner, closeness->data[i++].pawn->troop->owner));

			return neighbors_count;

		case ACTION_HOLD:
			neighbors_count += neighbors_static(position, ACTION_GUARD, neighbors + neighbors_count);
			break;
		}

	switch (pawn->action)
	{
	case ACTION_FIGHT:
		neighbors_count += neighbors_static(position, ACTION_HOLD, neighbors + neighbors_count);
		neighbors_count += neighbors_fight(game, pawn, position, closeness, neighbors + neighbors_count, NEIGHBOR_STATES_LIMIT - neighbors_count);
		break;

	case ACTION_ASSAULT:
	case ACTION_HOLD:
		// Add movement/assault neighbors.
		// TODO some of the neighbors may be unreachable (due to blockages)
		neighbors_count += neighbors_move(battle, pawn, position.x + 1, position.y, closeness, reachable, neighbors + neighbors_count);
		neighbors_count += neighbors_move(battle, pawn, position.x + M_SQRT2 / 2, position.y + M_SQRT2 / 2, closeness, reachable, neighbors + neighbors_count);
		neighbors_count += neighbors_move(battle, pawn, position.x, position.y - 1, closeness, reachable, neighbors + neighbors_count);
		neighbors_count += neighbors_move(battle, pawn, position.x - M_SQRT2 / 2, position.y - M_SQRT2 / 2, closeness, reachable, neighbors + neighbors_count);
		neighbors_count += neighbors_move(battle, pawn, position.x - 1, position.y, closeness, reachable, neighbors + neighbors_count);
		neighbors_count += neighbors_move(battle, pawn, position.x - M_SQRT2 / 2, position.y + M_SQRT2 / 2, closeness, reachable, neighbors + neighbors_count);
		neighbors_count += neighbors_move(battle, pawn, position.x, position.y + 1, closeness, reachable, neighbors + neighbors_count);
		neighbors_count += neighbors_move(battle, pawn, position.x + M_SQRT2 / 2, position.y + M_SQRT2 / 2, closeness, reachable, neighbors + neighbors_count);

		// Add the closest fight neighbors.
		neighbors_count += neighbors_fight(game, pawn, position, closeness, neighbors + neighbors_count, NEIGHBOR_STATES_LIMIT - neighbors_count);

		break;
	}

	return neighbors_count;
}

static void battle_state_set(struct pawn *restrict pawn, struct pawn_command *restrict neighbor, const struct game *restrict game, struct battle *restrict battle, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, struct position *restrict position)
{
	pawn_stay(pawn);
	pawn->action = 0;

	// TODO what if the destination is unreachable due to wall
	if (!position_eq(neighbor->position, pawn->position))
		if (movement_queue(pawn, neighbor->position, graph, obstacles) < 0)
			goto finally;

	// TODO what to do when setting the state fails
	switch (neighbor->action)
	{
	case ACTION_HOLD:
		pawn->action = neighbor->action;
		break;

	case ACTION_GUARD:
		pawn->action = neighbor->action;
		pawn->target.position = neighbor->target.position;
		break;

	case ACTION_FIGHT:
		combat_fight(game, battle, obstacles, pawn, neighbor->target.pawn);
		break;

	case ACTION_ASSAULT:
		combat_assault(game, pawn, neighbor->target.field);
		break;

	case ACTION_SHOOT:
		combat_shoot(game, battle, obstacles, pawn, neighbor->target.position);
		break;
	}

finally:
	*position = neighbor->position;
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
static double battle_state_rating(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct position *restrict positions, struct heap_pawn_distance *restrict closeness)
{
	size_t i, j;

	const struct pawn *victims[8]; // TODO is this big enough?
	unsigned victims_count;
	const struct garrison_info *info;

	double rating = 0.0, rating_max = 0.0;

	// TODO verify that rating_max does not depend on the current state
	// TODO think whether subtracting from rating is a good idea

	info = garrison_info(battle->region);

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
			printf("shoot %f (%f,%f)\n", attack_rating(damage_expected_ranged(pawn, pawn->count, victim), victim->count, victim->troop->unit, info), pawn->position.x, pawn->position.y);
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
				printf("fight %f (%f,%f)\n", attack_rating(damage_expected(pawn, (double)pawn->count / victims_count, victims[j]), victims[j]->count, victims[j]->troop->unit, info), pawn->position.x, pawn->position.y);
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
				printf("shoot closeness %f (%f,%f)\n", attack_impact / (distance / pawn->troop->unit->ranged.range + 1), other->position.x, other->position.y);
				rating_max += attack_impact + attack_impact / (distance_min / pawn->troop->unit->ranged.range + 1);

				other_count = ((other_count >= deaths) ? other_count - deaths : 0); // take into account the enemy troops that will die from the shooting
			}
			attack_impact = attack_rating(damage_expected(pawn, pawn->count, other), other_count, other->troop->unit, info);
			if (!pawn->troop->unit->ranged.weapon)
			{
				rating += attack_impact / (distance / pawn->troop->unit->speed + 1);
				printf("fight closeness %f (%f,%f)\n", attack_impact / (distance / pawn->troop->unit->speed + 1), other->position.x, other->position.y);
			}
			rating_max += attack_impact + attack_impact / (distance_min / pawn->troop->unit->speed + 1);

			// Subtract rating for future possibility of being attacked.
			if (other->troop->unit->ranged.weapon)
				rating -= attack_rating(damage_expected_ranged(other, other_count, pawn), pawn->count, pawn->troop->unit, info) / (distance / other->troop->unit->ranged.range + 1);
			else
			{
				rating -= attack_rating(damage_expected(other, other_count, pawn), pawn->count, pawn->troop->unit, info) / (distance / other->troop->unit->speed + 1);
				printf("enemy closeness -%f (%f,%f)\n", attack_rating(damage_expected(other, other_count, pawn), pawn->count, pawn->troop->unit, info) / (distance / other->troop->unit->speed + 1), other->position.x, other->position.y);
			}
		}

		// Add rating for future possibility of assaulting.
		// TODO do this only for siege machines (make a function that checks whether a unit is a siege machine)
		for(size_t y = 0; y < BATTLEFIELD_HEIGHT; ++y)
			for(size_t x = 0; x < BATTLEFIELD_WIDTH; ++x)
			{
				const struct battlefield *restrict field = &battle->field[y][x];
				struct position field_position;

				if ((field->blockage != BLOCKAGE_WALL) && (field->blockage != BLOCKAGE_GATE))
					continue;

				field_position = (struct position){x + 0.5, y + 0.5};
				distance = battlefield_distance(pawn_position, field_position);
				distance_min = battlefield_distance(pawn->position, field_position) - pawn->troop->unit->speed;
				if (distance_min < 0) distance_min = 0;

				attack_impact = attack_rating(damage_expected_assault(pawn, pawn->count, field), 1, field->unit, info);
				rating += attack_impact / (distance / pawn->troop->unit->speed + 1);

				rating_max += attack_impact + attack_impact / (distance_min / pawn->troop->unit->speed + 1);

				// TODO ally walls close to the current position (if on the two diagonals, large bonus (gate blocking))
			}
	}

	// Estimate how bad will the damage effects from enemies be.
	for(i = 0; i < battle->pawns_count; ++i) // loop enemy pawns
	{
		const struct pawn *restrict enemy = battle->pawns + i;
		double fight_rating;

		if (!enemy->count) continue;
		if (allies(game, enemy->troop->owner, player)) continue;

		// TODO guess when a pawn will prefer shoot/assault or fighting specific target

		// Adjust rating for damage from fighting.
		fight_rating = 0;
		victims_count = victims_fight_find(game, battle, enemy, positions, victims);
		for(j = 0; j < victims_count; ++j)
		{
			if (victims[j]->troop->owner != player)
				continue; // skip pawns owned by other players

			fight_rating -= attack_rating(damage_expected(enemy, (double)enemy->count / victims_count, victims[j]), victims[j]->count, victims[j]->troop->unit, info);
		}
		printf("enemy fight %f (%f,%f)\n", fight_rating, enemy->position.x, enemy->position.y);
		rating += fight_rating;

		// Adjust rating for damage from shooting.
		for(j = 0; j < battle->pawns_count; ++j)
		{
			const struct pawn *restrict victim = battle->pawns + j;

			if (!victim->count) continue;
			if (victim->troop->owner != player) continue;

			// TODO check if the pawn will be able to shoot (will there be enemies in fighting distance)
			if (enemy->troop->unit->ranged.weapon && (battlefield_distance(positions[i], positions[j]) <= enemy->troop->unit->ranged.range))
				rating -= attack_rating(damage_expected_ranged(enemy, enemy->count, victim), victim->count, victim->troop->unit, info);
		}
	}

/*
	if (battle->pawns[1].path.count)
		printf("%f (%f,%f)\n", rating / rating_max, battle->pawns[1].path.data[0].x, battle->pawns[1].path.data[0].y);
	else
		printf("%f (%f,%f)\n", rating / rating_max, battle->pawns[1].position.x, battle->pawns[1].position.y);
*/

	assert(rating_max);
printf("rating=%f rating_max=%f\n", rating, rating_max);
	return rating / rating_max;
}

int computer_formation(const struct game *restrict game, struct battle *restrict battle, unsigned char player)
{
	// TODO implement this
	return 0;
}

static int command_remember(struct pawn_command *restrict command, struct pawn *restrict pawn, struct position *restrict position)
{
	if (pawn->path.count)
		memcpy(command->path.data, pawn->path.data, pawn->path.count * sizeof(*pawn->path.data));
	command->path.count = pawn->path.count;

	command->action = pawn->action;
	switch (pawn->action)
	{
	case ACTION_FIGHT:
		command->target.pawn = pawn->target.pawn;
		break;

	case ACTION_GUARD:
	case ACTION_SHOOT:
		command->target.position = pawn->target.position;
		break;

	case ACTION_ASSAULT:
		command->target.field = pawn->target.field;
		break;
	}

	command->position = *position;

	return 0;
}

static void command_restore(struct pawn *restrict pawn, struct pawn_command *restrict command, struct position *restrict position)
{
	if (command->path.count)
		memcpy(pawn->path.data, command->path.data, command->path.count * sizeof(*command->path.data));
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

	*position = command->position;
}

// Determine the behavior of the computer using simulated annealing.
int computer_battle(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	double rating, rating_new;
	double temperature = 1.0;

	size_t pawns_count = battle->players[player].pawns_count;
	struct pawn_command backup = {0};

	double (*reachable)[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH] = {0};
	struct heap_pawn_distance *closeness = 0;
	struct position *positions = 0;

	struct pawn_command neighbors[NEIGHBOR_STATES_LIMIT];
	unsigned neighbors_count;

	struct pawn *restrict pawn;
	size_t i, j;
	size_t pawn_index;
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

	closeness = closeness_index(battle);
	if (!closeness) goto finally;

	// Predict what position will each pawn occupy after the move.
	// WARNING: Assume the posisions, where the pawns are commanded to go, will not be occupied. // TODO this assumption may cause problems
	// TODO do a better prediction
	positions = malloc(battle->pawns_count * sizeof(*positions));
	if (!positions) goto finally;
	for(i = 0; i < battle->pawns_count; ++i)
	{
		if (!battle->pawns[i].count) continue;
		positions[i] = battle->pawns[i].position;
	}

	// Choose suitable commands for the pawns of the player.
	rating = battle_state_rating(game, battle, player, positions, closeness);
	for(unsigned step = 0; step < ANNEALING_STEPS; ++step)
	{
		i = random() % pawns_count;
		pawn = battle->players[player].pawns[i];
		pawn_index = pawn - battle->pawns;

		neighbors_count = battle_state_neighbors(game, battle, pawn, reachable[i], neighbors, closeness + pawn_index);
		if (!neighbors_count) continue;

		// Remember current pawn command and set a new one.
		status = command_remember(&backup, pawn, positions + pawn_index);
		if (status < 0) goto finally;
		battle_state_set(pawn, neighbors + (random() % neighbors_count), game, battle, graph, obstacles, positions + pawn_index);
		closeness_index_update(closeness, battle, positions, pawn_index);

		// Calculate the rating of the new set of commands.
		// Restore the original command if the new one is unacceptably worse.
		rating_new = battle_state_rating(game, battle, player, positions, closeness);
		if (state_wanted(rating, rating_new, temperature))
		{
			rating = rating_new;
//			printf("rating=%f\n", rating);
		}
		else
		{
			command_restore(pawn, &backup, positions + pawn_index);
			closeness_index_update(closeness, battle, positions, pawn - battle->pawns);
		}

		temperature *= ANNEALING_COOLDOWN;
	}

	// Find the local maximum (best action) for each of the pawns.
//	printf("GOING for local maximum: %f\n", rating);
	for(i = 0; i < pawns_count; ++i)
	{
		pawn = battle->players[player].pawns[i];
		pawn_index = pawn - battle->pawns;

search:
		neighbors_count = battle_state_neighbors(game, battle, pawn, reachable[i], neighbors, closeness + pawn_index);
		for(j = 0; j < neighbors_count; ++j)
		{
			// Remember current pawn command and set a new one.
			status = command_remember(&backup, pawn, positions + pawn_index);
			if (status < 0) goto finally;
			battle_state_set(pawn, neighbors + j, game, battle, graph, obstacles, positions + pawn_index);

			// Calculate the rating of the new set of commands.
			// Restore the original command if the new one is unacceptably worse.
			rating_new = battle_state_rating(game, battle, player, positions, closeness);
			if (rating_new > rating)
			{
				rating = rating_new;
//				printf("rating=%f\n", rating);
				goto search; // state changed; search for neighbors of the new state
			}
			else command_restore(pawn, &backup, positions + pawn_index);
		}
	}

	status = 0;

finally:
	free(positions);
	free(closeness);
	free(reachable);
	return status;
}

double rate(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	double rating;

	struct heap_pawn_distance *closeness = 0;
	struct position *positions = 0;

	closeness = closeness_index(battle);
	if (!closeness) return NAN;

	// Predict what position will each pawn occupy after the move.
	// WARNING: Assume the posisions, where the pawns are commanded to go, will not be occupied. // TODO this assumption may cause problems
	// TODO do a better prediction
	positions = malloc(battle->pawns_count * sizeof(*positions));
	if (!positions)
	{
		free(closeness);
		return NAN;
	}
	for(size_t i = 0; i < battle->pawns_count; ++i)
	{
		if (!battle->pawns[i].count) continue;
		positions[i] = battle->pawns[i].position;
	}

	rating = battle_state_rating(game, battle, player, positions, closeness);

	return rating;
}

unsigned calculate_battle(const struct game *restrict game, struct region *restrict region, int assault)
{
	struct troop *restrict troop;
	unsigned winner_alliance = 0;

	// TODO make this work better for assault (improve unit importance - vanguard attack units are weaker; shooters are maybe stronger...)

	// Calculate the strength of each alliance participating in the battle.
	double strength[PLAYERS_LIMIT] = {0};
	for(troop = region->troops; troop; troop = troop->_next)
	{
		if (assault && !allies(game, troop->owner, region->garrison.owner))
			strength[game->players[troop->owner].alliance] += unit_importance(troop->unit, garrison_info(region)) * troop->count;
		else
			strength[game->players[troop->owner].alliance] += unit_importance(troop->unit, 0) * troop->count;
	}

	// Calculate total strength and find the strongest alliance.
	double strength_total = 0.0;
	unsigned alliances_count = 0;
	for(size_t i = 0; i < PLAYERS_LIMIT; ++i)
	{
		if (!strength[i]) continue;

		strength_total += strength[i];
		alliances_count += 1;
		if (strength[i] > strength[winner_alliance])
			winner_alliance = i;
	}

	// Adjust troops count.
	// TODO use a real formula here (with some randomness)
	double count_factor = 1 - (strength_total - strength[winner_alliance]) / ((alliances_count - 1) * strength[winner_alliance]);
	assert(count_factor <= 1);
	for(troop = region->troops; troop; troop = troop->_next)
	{
		if (game->players[troop->owner].alliance == winner_alliance)
			troop->count *= count_factor;
		else
			troop->count = 0;
	}

	return winner_alliance;
}
