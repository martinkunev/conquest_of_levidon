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

#include "game.h"
#include "draw.h"
#include "map.h"
#include "pathfinding.h"
#include "movement.h"
#include "battle.h"
#include "combat.h"

#define ACCURACY 0.5

#define MISS_MAX 20 /* 20% */

const double damage_boost[7][6] =
{
// ARMOR:					NONE		LEATHER		CHAINMAIL	PLATE		WOODEN		STONE
	[WEAPON_NONE] =	{		0.0,		0.0,		0.0,		0.0,		0.0,		0.0},
	[WEAPON_CLUB] = {		1.0,		0.8,		0.7,		0.2,		0.0,		0.0},
	[WEAPON_ARROW] = {		1.0,		0.8,		0.7,		0.2,		0.0,		0.0},
	[WEAPON_CLEAVING] = {	1.0,		0.9,		0.7,		0.3,		0.5,		0.0},
	[WEAPON_POLEARM] = {	1.0,		1.0,		0.7,		0.4,		0.2,		0.0},
	[WEAPON_BLADE] = {		1.0,		1.0,		0.8,		0.5,		0.2,		0.0},
	[WEAPON_BLUNT] = {		1.0,		1.0,		1.0,		0.5,		1.0,		0.7},
};

double damage_expected(const struct pawn *restrict fighter, double troops_count, const struct pawn *restrict victim)
{
	enum weapon weapon = fighter->troop->unit->melee.weapon;
	enum armor armor = victim->troop->unit->armor;
	double damage = fighter->troop->unit->melee.damage * fighter->troop->unit->melee.agility;
	return troops_count * damage * damage_boost[weapon][armor];
}

// TODO this is not that simple - it should take into account accuracy and damage to neighboring fields
double damage_expected_ranged(const struct pawn *restrict shooter, double troops_count, const struct pawn *restrict victim)
{
	enum weapon weapon = shooter->troop->unit->ranged.weapon;
	enum armor armor = victim->troop->unit->armor;
	double damage = shooter->troop->unit->ranged.damage;
	return troops_count * damage * damage_boost[weapon][armor];
}

static unsigned deaths(unsigned damage, unsigned troops, unsigned health)
{
	unsigned min, max;

	if ((health - 1) * troops >= damage) min = 0;
	else min = damage;
	max = damage / health;
	if (max > troops) max = troops;
	if (min > max) min = max;

	return min + random() % (max - min + 1);
}

static void damage_deal(double damage, unsigned attacker_troops, struct pawn *restrict victim)
{
	const struct unit *restrict unit = victim->troop->unit;

	// Attacking troops can sometimes miss.
	damage *= (100 - random() % (MISS_MAX + 1)) / 100.0;

	// Each attacker deals damage to a single troop.
	// The attacker cannot deal more damage than the health of the troop.
	if (damage > unit->health) damage = unit->health;

	unsigned damage_final = (unsigned)(damage * attacker_troops + 0.5);

	unsigned defender_troops = victim->count;
	if (defender_troops > attacker_troops) defender_troops = attacker_troops;

	unsigned dead = deaths(damage_final, defender_troops, unit->health);
	victim->dead += dead;
	victim->hurt += damage_final - (dead * unit->health);
}

static void assault(const struct pawn *restrict fighter, struct battlefield *restrict target)
{
	enum armor armor = target->armor;
	enum weapon weapon = fighter->troop->unit->melee.weapon;

	unsigned damage_final = (unsigned)(fighter->troop->unit->melee.damage * fighter->count * damage_boost[weapon][armor] + 0.5);

	if (damage_final >= target->strength) target->strength = 0; // TODO handle towers
	else target->strength -= damage_final;
}

static void fight(const struct pawn *restrict fighter, struct pawn *restrict victims[], size_t victims_count)
{
	// assert(victims_count <= 8);

	size_t i;

	enum weapon weapon = fighter->troop->unit->melee.weapon;
	double damage = fighter->troop->unit->melee.damage * fighter->troop->unit->melee.agility;

	// Distribute the attacking troops equally between the victims.
	// Choose targets for the remainder randomly.
	unsigned attackers = fighter->count / victims_count;
	unsigned targets_attackers[8] = {0};
	unsigned left = fighter->count % victims_count;
	while (left--)
		targets_attackers[random() % victims_count] += 1;

	for(i = 0; i < victims_count; ++i)
	{
		targets_attackers[i] += attackers;
		damage_deal(damage * damage_boost[weapon][victims[i]->troop->unit->armor], targets_attackers[i], victims[i]);
	}
}

static void shoot(const struct pawn *shooter, struct pawn *victim, double damage, double miss, double distance_victim)
{
	// The larger the distance to the target, the more the damage is spread around it.

	// Shoot accuracy at the center and at the periphery of the target area for minimum and maximum distance.
	static const double target_accuracy[2][2] = {{1, 0.5}, {0, 0.078125}}; // {{1, 1/2}, {0, 5/64}}

	enum armor armor = victim->troop->unit->armor;
	enum weapon weapon = shooter->troop->unit->ranged.weapon;

	double on_target_center = target_accuracy[0][0] * (1 - miss) + target_accuracy[0][1] * miss;
	double on_target_periphery = target_accuracy[1][0] * (1 - miss) + target_accuracy[1][1] * miss;

	double miss_victim = distance_victim / DISTANCE_RANGED;
	damage *= on_target_center * (1 - miss_victim) + on_target_periphery * miss_victim;
	damage *= damage_boost[weapon][armor];

	// Assume all troops can shoot this victim.
	damage_deal(damage, shooter->count, victim);
}

static inline int battlefield_reachable(struct position origin, struct position target, double distance_max)
{
	double distance = battlefield_distance(origin, target);
	return (distance <= distance_max);
}

void battle_fight(const struct game *restrict game, struct battle *restrict battle) // TODO rename to something like combat_melee
{
	size_t i, j;
	for(i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *fighter = battle->pawns + i;
		unsigned char fighter_alliance = game->players[fighter->troop->owner].alliance;

		if (!fighter->count) continue;
		if (fighter->action == ACTION_SHOOT) continue;

		if (fighter->action == ACTION_ASSAULT)
		{
			// Determine whether the target field is close enough to be attacked.

			// TODO maybe reduce the size of the target to the actual size of the wall (use WALL_OFFSET)

			struct tile tile = fighter->target.field->tile;

			float distance_left = fabs(tile.x - fighter->position.x), distance_right = fabs(tile.x + 1 - fighter->position.x);
			float distance_bottom = fabs(tile.y - fighter->position.y), distance_top = fabs(tile.y + 1 - fighter->position.y);
			float dx = ((distance_left <= distance_right) ? distance_left : distance_right);
			float dy = ((distance_bottom <= distance_top) ? distance_bottom : distance_top);

			if (dx * dx + dy * dy <= DISTANCE_MELEE * DISTANCE_MELEE)
				assault(fighter, fighter->target.field);
		}
		else
		{
			struct pawn *victims[8];
			unsigned victims_count = 0;

			// TODO what if one of the pawns is on a tower

			// If the pawn has a specific fight target and is able to fight it, fight only that target.
			// Otherwise, fight all enemy pawns nearby.
			if ((fighter->action == ACTION_FIGHT) && battlefield_reachable(fighter->position, fighter->target.pawn->position, DISTANCE_MELEE))
			{
				victims[victims_count++] = fighter->target.pawn;
			}
			else for(j = 0; j < battle->pawns_count; ++j)
			{
				struct pawn *victim = battle->pawns + j;
				if (game->players[victim->troop->owner].alliance == fighter_alliance)
					continue;
				if (battlefield_reachable(fighter->position, victim->position, DISTANCE_MELEE))
					victims[victims_count++] = victim;
			}

			fight(fighter, victims, victims_count);
		}
	}
}

void battle_shoot(struct battle *restrict battle, const struct obstacles *restrict obstacles)
{
	// Shooters deal damage in an area around the target.
	// There is friendly fire.

	size_t i, j;
	for(i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *shooter = battle->pawns + i;

		if (!shooter->count) continue;
		if (shooter->action != ACTION_SHOOT) continue;

		double damage_total = shooter->troop->unit->ranged.damage;
		unsigned range;
		double distance, miss;

		// TODO what if there is a tower on one of the fields
		// TODO add option for the shooters to spread their arrows

		range = shooter->troop->unit->ranged.range;
		if (!path_visible(shooter->position, shooter->target.position, obstacles))
		{
			damage_total *= ACCURACY;
			range -= 1;
		}
		distance = battlefield_distance(shooter->position, shooter->target.position);
		if (range < distance) continue; // no shooting if the target is too far // TODO is this necessary?

		miss = distance / shooter->troop->unit->ranged.range;

		for(j = 0; j < battle->pawns_count; ++j)
		{
			struct pawn *victim = battle->pawns + j;
			double distance_victim = battlefield_distance(shooter->target.position, victim->position);
			if (distance_victim <= DISTANCE_RANGED)
				shoot(shooter, victim, damage_total, miss, distance_victim);
		}
	}
}

int battlefield_clean(struct battle *battle)
{
	size_t p;
	size_t x, y;

	int activity = 0;

	// Remove destroyed obstacles.
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
		for(x = 0; x < BATTLEFIELD_HEIGHT; ++x)
		{
			struct battlefield *restrict field = &battle->field[y][x];
			if ((field->blockage == BLOCKAGE_OBSTACLE) && !field->strength)
			{
				activity = 1;
				field->blockage = BLOCKAGE_NONE;
			}
		}

	// Remove dead pawns and reset pawn actions.
	for(p = 0; p < battle->pawns_count; ++p)
	{
		struct pawn *pawn = battle->pawns + p;
		unsigned health = pawn->troop->unit->health;

		if (!pawn->count) continue;

		unsigned dead = deaths(pawn->hurt, pawn->count, health);
		if (dead) activity = 1;

		pawn->dead += dead;
		pawn->hurt -= dead * health;

		if (pawn->count <= pawn->dead)
		{
			// Remove dead pawns from the battlefield.
			// TODO remove pawn pointer from struct battlefield
			pawn->count = 0;
		}
		else pawn->count -= pawn->dead;
		pawn->dead = 0;

		// Stop pawns from attacking dead pawns and destroyed obstacles.
		if (pawn->count)
		{
			if ((pawn->action == ACTION_FIGHT) && !pawn->target.pawn->count)
				pawn->action = 0;

			if ((pawn->action == ACTION_ASSAULT) && !pawn->target.field->blockage)
				pawn->action = 0;
		}
	}

	return activity;
}

int combat_order_fight(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct pawn *restrict victim)
{
	// TODO verify that the target is reachable
	// TODO what if one of the pawns is on a tower

	if (allies(game, fighter->troop->owner, victim->troop->owner))
		return 0;

	fighter->action = ACTION_FIGHT;
	fighter->target.pawn = victim;

	return 1;
}

int combat_order_assault(const struct game *restrict game, struct pawn *restrict fighter, struct battlefield *restrict target)
{
	// TODO verify that the target is reachable
	// TODO what if the fighter attacks a tower with pawn on it

	if ((target->blockage != BLOCKAGE_OBSTACLE) || allies(game, fighter->troop->owner, target->owner)) // TODO the allies check works only for gates
		return 0;

	fighter->action = ACTION_ASSAULT;
	fighter->target.field = target;

	return 1;
}

int combat_order_shoot(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict shooter, struct position target)
{
	size_t i;

	// Only ranged units can shoot.
	if (!shooter->troop->unit->ranged.weapon) return 0;

	// Don't allow sooting if there is a neighbor enemy pawn.
	// TODO loop only through the pawns in the area
	for(i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *pawn = battle->pawns + i;
		unsigned char fighter_alliance = game->players[shooter->troop->owner].alliance;

		if (!pawn->count) continue;

		if ((game->players[pawn->troop->owner].alliance != fighter_alliance) && battlefield_reachable(shooter->position, pawn->position, DISTANCE_MELEE))
			return 0;
	}

	// Check if the target is in shooting range.
	{
		unsigned range = shooter->troop->unit->ranged.range;

		// If there is an obstacle between the pawn and its target, decrease shooting range by 1.
		// TODO what if there is a tower on one of the fields
		if (!path_visible(shooter->position, target, obstacles))
			range -= 1;

		if (battlefield_distance(shooter->position, target) > range)
			return 0;
	}

	pawn_stay(shooter);
	shooter->action = ACTION_SHOOT;
	shooter->target.position = target;

	return 1;
}
