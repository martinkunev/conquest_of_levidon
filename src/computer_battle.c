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

static void distance_sort(struct heap_pawn_distance *closest)
{
	size_t pawns_count = closest->count;
	heap_pawn_distance_heapify(closest);
	while (closest->count)
	{
		struct pawn_distance pawn_distance = closest->data[0];
		heap_pawn_distance_pop(closest);
		closest->data[closest->count] = pawn_distance;
	}
	closest->count = pawns_count;
}

// Returns the number of pawns that are no farther than distance.
static size_t distance_search(const struct heap_pawn_distance *restrict closest, double distance)
{
	size_t left = 0, right = closest->count;

	while (left < right)
	{
		size_t index = (right - left) / 2 + left;
		if (closest->data[index].distance <= distance)
			left = index + 1;
		else
			right = index;
	}

	return left;
}

static struct heap_pawn_distance *closest_index(const struct battle *restrict battle)
{
	struct heap_pawn_distance *closest;

	unsigned char *buffer = malloc(battle->pawns_count * (sizeof(*closest) + (battle->pawns_count - 1) * sizeof(*closest->data)));
	if (!buffer) return 0;

	// For each pawn, initalize a list of the other pawns ordered by distance ascending.
	closest = (void *)buffer;
	buffer += battle->pawns_count * sizeof(*closest);
	for(size_t i = 0; i < battle->pawns_count; ++i)
	{
		if (!battle->pawns[i].count)
			continue;

		closest[i].count = 0;
		closest[i].data = (void *)buffer;
		buffer += (battle->pawns_count - 1) * sizeof(*closest->data);

		for(size_t j = 0; j < battle->pawns_count; ++j)
		{
			if (!battle->pawns[j].count)
				continue;
			if (j == i)
				continue;
			closest[i].data[closest[i].count].pawn = battle->pawns + j;
			closest[i].data[closest[i].count].distance = battlefield_distance(battle->pawns[i].position, battle->pawns[j].position);
			closest[i].count += 1;
		}

		// Sort the pawns by distance.
		distance_sort(closest + i);
	}

	return closest;
}

static void closest_index_update(struct heap_pawn_distance *restrict closest, const struct battle *restrict battle, const struct position *restrict positions, size_t index)
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
			for(j = 0; j < closest[i].count; ++j)
			{
				size_t index_current = closest[i].data[j].pawn - battle->pawns;
				closest[i].data[j].distance = battlefield_distance(positions[i], positions[index_current]);
			}

			distance_sort(closest + i);
		}
		else
		{
			// Find the pawn which changed position.
			distance = battlefield_distance(positions[i], positions[index]);
			for(j = 0; j < closest[i].count; ++j)
			{
				size_t index_current = closest[i].data[j].pawn - battle->pawns;
				if (index_current == index)
					break;
			}
			assert(j < closest[i].count);

			position = j;
			swap = closest[i].data[position];
			if (distance < swap.distance) // the pawn moved closer
			{
				// Determine which elements need to be shifted right.
				while (j && (distance < closest[i].data[j - 1].distance))
					j -= 1;
				if (j < position)
					memmove(closest[i].data + j + 1, closest[i].data + j, (position - j) * sizeof(*closest[i].data));
			}
			else
			{
				// Determine which elements need to be shifted left.
				while (((j + 1) < closest[i].count) && (distance > closest[i].data[j + 1].distance))
					j += 1;
				if (j > position)
					memmove(closest[i].data + position, closest[i].data + position + 1, (j - position) * sizeof(*closest[i].data));
			}
			swap.distance = distance;
			closest[i].data[j] = swap;
		}
	}
}

static unsigned neighbors_fight(const struct game *restrict game, const struct pawn *restrict pawn, struct position position, const struct heap_pawn_distance *restrict closest, struct pawn_command *neighbors, size_t neighbors_limit)
{
	size_t neighbors_count = 0;

	for(size_t i = 0; i < closest->count; ++i)
	{
		if (closest->data[i].distance > DISTANCE_MELEE)
			break; // the remaining targets are too far to fight
		if (allies(game, pawn->troop->owner, closest->data[i].pawn->troop->owner))
			continue; // target is an ally
		if ((pawn->action == ACTION_FIGHT) && (closest->data[i].pawn == pawn->target.pawn))
			continue; // don't add the current state as neighbor

		neighbors->action = ACTION_FIGHT;
		neighbors->target.pawn = closest->data[i].pawn;
		neighbors->position = position;

		neighbors_count += 1;
		if (neighbors_count == neighbors_limit)
			break;
	}

	return neighbors_count;
}

static unsigned neighbors_shoot(const struct game *restrict game, const struct pawn *restrict pawn, struct position position, const struct heap_pawn_distance *restrict closest, struct pawn_command neighbors[static NEIGHBOR_STATES_LIMIT - 1], const struct obstacles *restrict obstacles)
{
	unsigned char range = pawn->troop->unit->ranged.range;

	unsigned neighbors_count = 0;

	if (!pawn->troop->unit->ranged.weapon)
		return 0; // TODO why did I delete this?

	for(size_t i = 0; i < closest->count; ++i)
	{
		const struct pawn *restrict target = closest->data[i].pawn;
		double distance = closest->data[i].distance;

		if (distance > range)
			break; // the remaining targets are too far to shoot
		if (allies(game, pawn->troop->owner, target->troop->owner))
			continue; // target is an ally
		if (distance <= DISTANCE_MELEE)
			break; // a nearby enemy is stopping the pawn from shooting
		if ((pawn->action == ACTION_SHOOT) && position_eq(pawn->target.position, target->position))
			continue; // don't add the current state as neighbor
		if (!path_visible(position, target->position, obstacles) && (distance > (range - 1)))
			continue; // shooting range is shorter if there is an obstacle between the pawn and the target

		neighbors[neighbors_count].action = ACTION_SHOOT;
		neighbors[neighbors_count].target.position = target->position;
		neighbors[neighbors_count].position = pawn->position;

		neighbors_count += 1;
		if (neighbors_count == NEIGHBOR_STATES_LIMIT - 1)
			break;
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

static unsigned neighbors_move(const struct battle *restrict battle, const struct pawn *restrict pawn, struct position *restrict positions, double x, double y, const struct heap_pawn_distance *restrict closest, double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct pawn_command neighbors[static 1])
{
	struct position destination = {x, y};
	struct tile tile = {(unsigned)x, (unsigned)y};
	const struct battlefield *restrict field;

	if (!in_battlefield(x, y))
		return 0;
	if (pawn->troop->unit->speed + PAWN_RADIUS < reachable[tile.y][tile.x])
		return 0; // destination is too far

	field = &battle->field[tile.y][tile.x];
	switch (field->blockage)
	{
	case BLOCKAGE_WALL:
	case BLOCKAGE_GATE:
		neighbors->action = ACTION_ASSAULT;
		neighbors->target.field = (struct battlefield *)field; // TODO fix this cast
		neighbors->position = positions[pawn - battle->pawns];
		return 1;

	case BLOCKAGE_NONE:
		// Don't add move if the destination is occupied by another pawn.
		for(size_t i = 0; i < closest->count; ++i)
		{
			if (closest->data[i].distance >= (1 + PAWN_RADIUS * 2))
				break; // the rest of the pawns are too far to occupy destination
			if (pawns_collide(destination, positions[closest->data[i].pawn - battle->pawns]))
				return 0;
		}

		neighbors->action = ACTION_HOLD;
		neighbors->position = destination;
		return 1;

	default:
		return 0;
	}
}

static unsigned battle_state_neighbors(const struct game *restrict game, const struct battle *restrict battle, struct position *restrict positions, struct pawn *restrict pawn, double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct pawn_command neighbors[static NEIGHBOR_STATES_LIMIT], struct heap_pawn_distance *restrict closest, const struct obstacles *restrict obstacles)
{
	struct position position = (pawn->path.count ? *pawn->path.data : pawn->position);

	unsigned neighbors_count = 0;

	// WARNING: path is not set by this function

	if (!pawn->path.count)
		switch (pawn->action)
		{
		case ACTION_SHOOT:
			neighbors_count += neighbors_shoot(game, pawn, position, closest, neighbors + neighbors_count, obstacles);
			neighbors_count += neighbors_static(position, ACTION_GUARD, neighbors + neighbors_count);
			return neighbors_count;

		case ACTION_GUARD:
			neighbors_count += neighbors_static(position, ACTION_HOLD, neighbors + neighbors_count);
			if (pawn->troop->unit->ranged.weapon)
				neighbors_count += neighbors_shoot(game, pawn, position, closest, neighbors + neighbors_count, obstacles);
			return neighbors_count;

		case ACTION_HOLD:
			neighbors_count += neighbors_static(position, ACTION_GUARD, neighbors + neighbors_count);
			break;
		}

	switch (pawn->action)
	{
	case ACTION_FIGHT:
		neighbors_count += neighbors_static(position, ACTION_HOLD, neighbors + neighbors_count);
		neighbors_count += neighbors_fight(game, pawn, position, closest, neighbors + neighbors_count, NEIGHBOR_STATES_LIMIT - neighbors_count);
		break;

	case ACTION_ASSAULT:
	case ACTION_HOLD:
		// Add movement/assault neighbors.
		neighbors_count += neighbors_move(battle, pawn, positions, position.x + 1, position.y, closest, reachable, neighbors + neighbors_count);
		neighbors_count += neighbors_move(battle, pawn, positions, position.x + M_SQRT2 / 2, position.y - M_SQRT2 / 2, closest, reachable, neighbors + neighbors_count);
		neighbors_count += neighbors_move(battle, pawn, positions, position.x, position.y - 1, closest, reachable, neighbors + neighbors_count);
		neighbors_count += neighbors_move(battle, pawn, positions, position.x - M_SQRT2 / 2, position.y - M_SQRT2 / 2, closest, reachable, neighbors + neighbors_count);
		neighbors_count += neighbors_move(battle, pawn, positions, position.x - 1, position.y, closest, reachable, neighbors + neighbors_count);
		neighbors_count += neighbors_move(battle, pawn, positions, position.x - M_SQRT2 / 2, position.y + M_SQRT2 / 2, closest, reachable, neighbors + neighbors_count);
		neighbors_count += neighbors_move(battle, pawn, positions, position.x, position.y + 1, closest, reachable, neighbors + neighbors_count);
		neighbors_count += neighbors_move(battle, pawn, positions, position.x + M_SQRT2 / 2, position.y + M_SQRT2 / 2, closest, reachable, neighbors + neighbors_count);

		// Add the closest fight neighbors.
		neighbors_count += neighbors_fight(game, pawn, position, closest, neighbors + neighbors_count, NEIGHBOR_STATES_LIMIT - neighbors_count);

		break;
	}

	return neighbors_count;
}

static void battle_state_set(struct pawn *restrict pawn, struct pawn_command *restrict neighbor, const struct game *restrict game, const struct battle *restrict battle, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, struct position *restrict position)
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

static unsigned victims_find(const struct game *restrict game, const struct pawn *restrict fighter, const struct heap_pawn_distance *restrict closest, double distance, const struct pawn *restrict victims[static VICTIMS_LIMIT])
{
	unsigned char fighter_alliance = game->players[fighter->troop->owner].alliance;
	unsigned victims_count = 0;

	for(size_t i = 0; i < closest->count; ++i)
	{
		if (closest->data[i].distance > distance)
			break;
		if (game->players[closest->data[i].pawn->troop->owner].alliance != fighter_alliance)
		{
			victims[victims_count++] = closest->data[i].pawn;
			if (victims_count == VICTIMS_LIMIT)
				return victims_count;
		}
	}

	return victims_count;
}

// Returns the statistical expected value of deaths.
static inline double deaths_expected(double damage, const struct unit *restrict victim, unsigned victims_count)
{
	// TODO there may be less deaths when lots of pawns attack the same victim
	// TODO take into account number of attackers (see combat.c deaths())
	double deaths = 0.75 * (damage / victim->health);
	return ((deaths > victims_count) ? victims_count : deaths);
}

static inline double attack_rating(double damage, const struct unit *restrict victim, unsigned victims_count, const struct garrison_info *restrict info)
{
	return deaths_expected(damage, victim, victims_count) * unit_importance(victim, info);
}

static inline unsigned distance_rounds(double distance, double goal, unsigned char speed)
{
	return ceil((distance - goal) / speed);
}

static inline double distance_coefficient(double distance, double goal, unsigned char speed)
{
	distance = ((distance >= goal) ? (distance - goal) : 0);
	return 1 + distance / speed;
}

// TODO think how to optimize this (O(n^3) is too high)
static double battle_state_rating(const struct game *restrict game, const struct battle *restrict battle, unsigned char player, struct position *restrict positions, struct heap_pawn_distance *restrict closest, const struct obstacles *restrict obstacles)
{
	size_t i, j, k;

	const struct garrison_info *info;

	double offense = ((game->players[player].alliance == battle->defender) ? 1 : 1.1);
	double damage;
	unsigned *counts;

	double rating = 0.0, rating_max = 0.0;

	counts = malloc(battle->pawns_count * sizeof(*counts));
	if (!counts)
		return NAN;
	for(i = 0; i < battle->pawns_count; ++i)
		counts[i] = battle->pawns[i].count;

	// TODO think whether subtracting from rating is a good idea

	info = garrison_info(battle->region);

	// TODO don't run away from ranged units unless you're faster?
	// TODO think about allied troops

	// Estimate how beneficial is the command given to each of the player's pawns.
	for(i = 0; i < battle->players[player].pawns_count; ++i) // loop the pawns the player controls
	{
		const struct pawn *restrict pawn = battle->players[player].pawns[i];
		size_t pawn_index;
		if (!pawn->count)
			continue;

		pawn_index = pawn - battle->pawns;

		// TODO add to rating_max

		// WARNING: Assume the target will be reached in the following round.
		if (pawn->action == ACTION_SHOOT)
		{
			struct victim_shoot victims[VICTIMS_LIMIT];
			unsigned victims_count;
			double inaccuracy = combat_shoot_inaccuracy(pawn, obstacles);

			// estimate shoot impact
			victims_count = combat_shoot_victims(battle, pawn, victims);
			for(j = 0; j < victims_count; ++j)
			{
				double deaths;

				damage = victims[j].pawn->hurt + combat_shoot_damage(pawn, inaccuracy, victims[j].distance, victims[j].pawn);
				deaths = deaths_expected(damage, victims[j].pawn->troop->unit, victims[j].pawn->count);
				rating += deaths * unit_importance(victims[j].pawn->troop->unit, info);
				printf("shoot %f (%f,%f)\n", deaths * unit_importance(victims[j].pawn->troop->unit, info), pawn->position.x, pawn->position.y);

				// Take into account deaths for the count of the troops.
				counts[victims[j].pawn - battle->pawns] -= (unsigned)deaths;
			}
		}
		else if (pawn->action == ACTION_ASSAULT)
		{
			if (game->players[player].alliance == battle->defender)
				continue; // no rating for attacking own obstacles

			// estimate assault impact
			damage = combat_assault_damage(pawn, pawn->target.field);
			rating += attack_rating(damage, pawn->target.field->unit, 1, info);
			printf("fight %f (%f,%f)\n", attack_rating(damage, pawn->target.field->unit, 1, info), pawn->position.x, pawn->position.y);
		}
		else if (pawn->action == ACTION_FIGHT)
		{
			// estimate fight impact
			const struct pawn *restrict victim = pawn->target.pawn;
			damage = combat_fight_damage(pawn, combat_fight_troops(pawn, pawn->count, victim->count), victim);
			rating += attack_rating(damage, victim->troop->unit, victim->count, info);
			printf("fight %f (%f,%f)\n", attack_rating(damage, victim->troop->unit, victim->count, info), pawn->position.x, pawn->position.y);
		}
		else
		{
			double distance = ((pawn->action == ACTION_GUARD) ? guard_distance(pawn->troop->unit) : DISTANCE_MELEE);
			const struct pawn *victims[VICTIMS_LIMIT];
			unsigned victims_count = victims_find(game, pawn, closest + pawn_index, distance, victims);

			// estimate fight impact while guarding
			for(j = 0; j < victims_count; ++j)
			{
				damage = combat_fight_damage(pawn, combat_fight_troops(pawn, pawn->count, victims[j]->count), victims[j]);
				rating += attack_rating(damage, victims[j]->troop->unit, victims[j]->count, info) / (FIGHT_ERROR * victims_count);
				printf("maybe fight %f (%f,%f)\n", attack_rating(damage, victims[j]->troop->unit, victims[j]->count, info), pawn->position.x, pawn->position.y);
			}
		}
	}

	// Estimate future benefits and troubles.
	for(i = 0; i < battle->pawns_count; ++i)
	{
		double distance, distance_original;
		double impact;
		double rating_attack, rating_attack_max;

		const struct pawn *restrict attacker = battle->pawns + i;
		if (!attacker->count)
			continue;
		if (allies(game, attacker->troop->owner, player) && (attacker->troop->owner != player))
			continue; // skip pawns owned by allied players

		for(j = 0; j < closest[i].count; ++j)
		{
			const struct pawn *restrict victim = closest[i].data[j].pawn;
			size_t victim_index = victim - battle->pawns;
			struct position position_victim;
			if (allies(game, attacker->troop->owner, victim->troop->owner))
				continue;

			// TODO check if the pawn is really reachable for melee fight (no obstacles)
			// TODO check if the pawn will be able to shoot; take into account obstacles on the way and damage splitting to neighboring fields

			unsigned rounds_melee, rounds_ranged;
			unsigned defenders = 1, defenders_ranged = 1;

			position_victim = positions[victim_index];

			distance = closest[i].data[j].distance;
			distance_original = battlefield_distance(attacker->position, position_victim);

			rounds_melee = 1 + distance_rounds(distance, DISTANCE_MELEE, attacker->troop->unit->speed);
			if (attacker->troop->owner != player) // adjust melee rounds in case the victim is guarded
			{
				for(k = 0; k < closest[victim_index].count; ++k)
				{
					const struct pawn *restrict middleman = closest[victim_index].data[k].pawn;
					unsigned guard_distance;
					if (middleman->troop->owner != victim->troop->owner)
						continue;
					if (closest[victim_index].data[k].distance > distance_original)
						break; // the remaining pawns are too far to guard

					guard_distance = middleman->troop->unit->speed / 2.0;
					if ((middleman->action == ACTION_GUARD) && move_blocked_pawn(attacker->position, position_victim, positions[middleman - battle->pawns], guard_distance))
					{
						// TODO is this okay?
						rounds_melee += 1;
						break;
					}
				}
			}
			if (attacker->troop->unit->ranged.weapon)
				rounds_ranged = 1 + distance_rounds(distance, attacker->troop->unit->ranged.range, attacker->troop->unit->speed);

			for(k = 0; k < closest[victim_index].count; ++k)
			{
				const struct pawn_distance *restrict middleman = closest[victim_index].data + k;
				unsigned rounds_position; // number of rounds necessary to reach the calculated position
				if (middleman->pawn->troop->owner != victim->troop->owner)
					continue;

				rounds_position = (victim->troop->owner == player);
				if ((rounds_position + distance_rounds(middleman->distance, DISTANCE_MELEE, victim->troop->unit->speed + middleman->pawn->troop->unit->speed)) < rounds_melee)
					defenders += 1;
				if ((rounds_position + distance_rounds(middleman->distance + attacker->troop->unit->speed, DISTANCE_MELEE, victim->troop->unit->speed + middleman->pawn->troop->unit->speed)) < rounds_ranged)
					defenders_ranged += 1; // TODO this is supposed to indicate that ranged attack may not be possible
			}

			damage = combat_fight_damage(attacker, combat_fight_troops(attacker, attacker->count, counts[j]), victim);
			impact = attack_rating(damage, victim->troop->unit, counts[j], info);
			rating_attack = impact / (defenders * (1 + distance_coefficient(distance, DISTANCE_MELEE, attacker->troop->unit->speed)));
			if ((attacker->troop->owner == player) && (attacker->action != ACTION_FIGHT))
				rating_attack *= FIGHT_ERROR;
			rating_attack_max = impact / distance_coefficient(distance_original, DISTANCE_MELEE, attacker->troop->unit->speed);
			if (attacker->troop->unit->ranged.weapon)
			{
				double rating_ranged;

				// TODO make a better estimation for inaccuracy
				damage = combat_shoot_damage(attacker, 1, 0, victim);
				impact = attack_rating(damage, victim->troop->unit, counts[j], info);

				rating_ranged = impact / (defenders_ranged * (1 + distance_coefficient(distance, attacker->troop->unit->ranged.range, attacker->troop->unit->speed)));
				if (rating_ranged > rating_attack)
					rating_attack = rating_ranged;

				rating_ranged = impact / distance_coefficient(distance_original, attacker->troop->unit->ranged.range, attacker->troop->unit->speed);
				if (rating_ranged > rating_attack_max)
					rating_attack_max = rating_ranged;
			}

			// TODO maybe decrease rounds for ACTION_GUARD (since the attacker could attack earlier)

			if (attacker->troop->owner == player)
			{
				// Add rating for future possibility of attacking.
				rating += offense * rating_attack;
printf("future benefit %f: %.*s -> %.*s\n", offense * rating_attack, (int)attacker->troop->unit->name_length, attacker->troop->unit->name, (int)victim->troop->unit->name_length, victim->troop->unit->name);
				rating_max += offense * rating_attack_max;
			}
			else
			{
				// Subtract rating for future possibility of being attacked.
				rating -= rating_attack;
printf("future harm %f: %.*s <- %.*s\n", rating_attack, (int)victim->troop->unit->name_length, victim->troop->unit->name, (int)attacker->troop->unit->name_length, attacker->troop->unit->name);
			}
		}

		// TODO should I add rating below if the player is defending the garrison
		if (game->players[player].alliance == battle->defender)
			continue;

		// TODO add assault defense support
		// TODO a player may be guarding the obstacle
		for(j = 0; j < obstacles->count; ++j)
		{
			const struct obstacle *restrict obstacle = obstacles->obstacle + j;
			const struct battlefield *restrict field = &battle->field[(size_t)(obstacle->top + PAWN_RADIUS)][(size_t)(obstacle->left + PAWN_RADIUS)];

			distance = combat_assault_distance(positions[i], obstacle);
			distance_original = combat_assault_distance(attacker->position, obstacle);

			damage = combat_assault_damage(attacker, field);
			impact = attack_rating(damage, field->unit, 1, info);
			rating_attack = impact / (1 + distance_coefficient(distance, DISTANCE_MELEE, attacker->troop->unit->speed));
			rating_attack_max = impact / distance_coefficient(distance_original, DISTANCE_MELEE, attacker->troop->unit->speed);

			// Add rating for future possibility of assault.
			rating += offense * rating_attack;
			rating_max += offense * rating_attack_max;
		}

		// TODO if the walls are allied, add rating for entry blocking
	}

/*
	if (battle->pawns[1].path.count)
		printf("%f (%f,%f)\n", rating / rating_max, battle->pawns[1].path.data[0].x, battle->pawns[1].path.data[0].y);
	else
		printf("%f (%f,%f)\n", rating / rating_max, battle->pawns[1].position.x, battle->pawns[1].position.y);
*/

	free(counts);

	assert(rating_max);
printf("rating=%f rating_max=%f | %f\n", rating, rating_max, rating / rating_max);
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
	struct heap_pawn_distance *closest = 0;
	struct position *positions = 0;

	struct pawn_command neighbors[NEIGHBOR_STATES_LIMIT + NEIGHBOR_STATES_LIMIT - 1]; // TODO this is a temporary fix for adding shoot neighbors when looking for local maximum
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

	closest = closest_index(battle);
	if (!closest) goto finally;

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
	rating = battle_state_rating(game, battle, player, positions, closest, obstacles);
	for(unsigned step = 0; step < ANNEALING_STEPS; ++step)
	{
		i = random() % pawns_count;
		pawn = battle->players[player].pawns[i];
		pawn_index = pawn - battle->pawns;

		neighbors_count = battle_state_neighbors(game, battle, positions, pawn, reachable[i], neighbors, closest + pawn_index, obstacles);
		if (!neighbors_count) continue;

		// Remember current pawn command and set a new one.
		status = command_remember(&backup, pawn, positions + pawn_index);
		if (status < 0) goto finally;
		battle_state_set(pawn, neighbors + (random() % neighbors_count), game, battle, graph, obstacles, positions + pawn_index);
		closest_index_update(closest, battle, positions, pawn_index);

		// Calculate the rating of the new set of commands.
		// Restore the original command if the new one is unacceptably worse.
		rating_new = battle_state_rating(game, battle, player, positions, closest, obstacles);
		if (state_wanted(rating, rating_new, temperature))
		{
			rating = rating_new;
//			printf("rating=%f\n", rating);
		}
		else
		{
			command_restore(pawn, &backup, positions + pawn_index);
			closest_index_update(closest, battle, positions, pawn_index);
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
		neighbors_count = battle_state_neighbors(game, battle, positions, pawn, reachable[i], neighbors, closest + pawn_index, obstacles);
		neighbors_count += neighbors_shoot(game, pawn, positions[i], closest + pawn_index, neighbors + neighbors_count, obstacles); // TODO this is a temporary fix
		for(j = 0; j < neighbors_count; ++j)
		{
			// Remember current pawn command and set a new one.
			status = command_remember(&backup, pawn, positions + pawn_index);
			if (status < 0) goto finally;
			battle_state_set(pawn, neighbors + j, game, battle, graph, obstacles, positions + pawn_index);
			closest_index_update(closest, battle, positions, pawn_index);

			// Calculate the rating of the new set of commands.
			// Restore the original command if the new one is unacceptably worse.
			rating_new = battle_state_rating(game, battle, player, positions, closest, obstacles);
			if (rating_new > rating)
			{
				rating = rating_new;
//				printf("rating=%f\n", rating);
				goto search; // state changed; search for neighbors of the new state
			}
			else
			{
				command_restore(pawn, &backup, positions + pawn_index);
				closest_index_update(closest, battle, positions, pawn_index);
			}
		}

		// TODO we needed path for battle_state_rating() but it actually makes the action useless; get rid of this special casing
		if (pawn->action == ACTION_FIGHT)
			pawn->path.count = 0;
	}

	status = 0;

finally:
	free(positions);
	free(closest);
	free(reachable);
	return status;
}

double rate(const struct game *restrict game, const struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	double rating;

	struct heap_pawn_distance *closest = 0;
	struct position *positions = 0;

	closest = closest_index(battle);
	if (!closest) return NAN;

	// Predict what position will each pawn occupy after the move.
	// WARNING: Assume the posisions, where the pawns are commanded to go, will not be occupied. // TODO this assumption may cause problems
	// TODO do a better prediction
	positions = malloc(battle->pawns_count * sizeof(*positions));
	if (!positions)
	{
		free(closest);
		return NAN;
	}
	for(size_t i = 0; i < battle->pawns_count; ++i)
	{
		if (!battle->pawns[i].count) continue;
		positions[i] = battle->pawns[i].position;
	}
	for(size_t i = 0; i < battle->pawns_count; ++i)
	{
		if (!battle->pawns[i].count) continue;
		if (battle->pawns[i].path.count)
		{
			positions[i] = battle->pawns[i].path.data[0];
			closest_index_update(closest, battle, positions, i);
		}
	}

	rating = battle_state_rating(game, battle, player, positions, closest, obstacles);

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
