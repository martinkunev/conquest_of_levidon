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
#include <stdint.h>
#include <stdlib.h>

#include "game.h"
#include "draw.h"
#include "map.h"
#include "pathfinding.h"
#include "movement.h"
#include "battle.h"
#include "combat.h"

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

static inline unsigned combat_damage(unsigned attacker_troops, double damage, enum weapon weapon, const struct unit *restrict victim)
{
	damage *= damage_boost[weapon][victim->armor];

	// Each attacker deals damage to a single troop.
	// The attacker cannot deal more damage than the health of the troop.
	if (damage > victim->health) damage = victim->health;

	return (unsigned)(damage * attacker_troops + 0.5);
}

unsigned combat_fight_troops(const struct pawn *restrict fighter, unsigned fighter_troops, unsigned victim_troops)
{
	unsigned max;

	// No more than half the troops can attack a single pawn.
	max = fighter->troop->unit->troops_count / 2;
	if (fighter_troops > max)
		fighter_troops = max;

	// No more than two troops can attack a single troop.
	max = victim_troops * 2;
	if (fighter_troops > max)
		fighter_troops = max;

	return fighter_troops;
}

double combat_fight_damage(const struct pawn *restrict fighter, unsigned fighter_troops, const struct pawn *restrict victim)
{
	enum weapon weapon = fighter->troop->unit->melee.weapon;
	double damage = fighter->troop->unit->melee.damage * fighter->troop->unit->melee.agility;
	return combat_damage(fighter_troops, damage, weapon, victim->troop->unit);
}

// Returns the distance from a position to an obstacle.
double combat_assault_distance(const struct position position, const struct obstacle *restrict obstacle)
{
	float dx, dy;

	if (obstacle->left > position.x) dx = obstacle->left - position.x;
	else if (position.x > obstacle->right) dx = position.x - obstacle->right;
	else dx = 0;

	if (obstacle->top > position.y) dy = obstacle->top - position.y;
	else if (position.y > obstacle->bottom) dy = position.y - obstacle->bottom;
	else dy = 0;

	return sqrt(dx * dx + dy * dy);
}

double combat_assault_damage(const struct pawn *restrict fighter, const struct battlefield *restrict target)
{
	// No more than half the troops can attack the obstacle.
	double damage = combat_damage(fighter->count / 2, fighter->troop->unit->melee.damage, fighter->troop->unit->melee.weapon, target->unit);
	if (damage > target->strength)
		damage = target->strength;
	return damage;
}

unsigned combat_shoot_victims(struct battle *restrict battle, const struct pawn *restrict shooter, struct victim_shoot victims[static VICTIMS_LIMIT])
{
	// Shooters deal damage in an area around the target.
	// There is friendly fire.

	// TODO use closest index to loop through the pawns (don't loop through all of them)

	unsigned victims_count = 0;
	for(size_t i = 0; i < battle->pawns_count; ++i)
	{
		double distance = battlefield_distance(shooter->target.position, battle->pawns[i].position);
		if (distance > DISTANCE_RANGED)
			continue;

		victims[victims_count].pawn = battle->pawns + i;
		victims[victims_count].distance = distance;
		victims_count += 1;
	}
	return victims_count;
}

double combat_shoot_inaccuracy(const struct pawn *restrict shooter, const struct obstacles *restrict obstacles)
{
	double distance = battlefield_distance(shooter->position, shooter->target.position);
	unsigned range = shooter->troop->unit->ranged.range;

	if (!path_visible(shooter->position, shooter->target.position, obstacles))
		range -= 1;

	return distance / range;
}

double combat_shoot_damage(const struct pawn *restrict shooter, double inaccuracy, double distance_victim, const struct pawn *restrict victim)
{
	// The larger the distance to the target, the more the damage is spread around it.

	// Shoot accuracy at the center and at the periphery of the target area for minimum and maximum distance.
	static const double target_accuracy[2][2] = {{1, 0.5}, {0, 0.078125}}; // {{1, 1/2}, {0, 5/64}}

	double impact_center = target_accuracy[0][0] * (1 - inaccuracy) + target_accuracy[0][1] * inaccuracy;
	double impact_periphery = target_accuracy[1][0] * (1 - inaccuracy) + target_accuracy[1][1] * inaccuracy;

	double damage = combat_damage(shooter->count, shooter->troop->unit->ranged.damage, shooter->troop->unit->ranged.weapon, victim->troop->unit);
	double offtarget = distance_victim / DISTANCE_RANGED;

	return damage * impact_center * (1 - offtarget) + impact_periphery * offtarget;
}

static unsigned deaths(const struct pawn *restrict pawn)
{
	unsigned deaths_max, deaths_min;
	unsigned hurt_withstand;
	unsigned deaths_certain;
	double deaths_actual;

	// Maximum deaths are achieved when the damage is concentrated to single troops.
	deaths_max = pawn->hurt / pawn->troop->unit->health;

	// Each attacker can kill at most 1 troop.
	if (deaths_max > pawn->attackers) deaths_max = pawn->attackers;

	// Minimum deaths are achieved when the damage is spread between troops.
	hurt_withstand = (pawn->troop->unit->health - 1) * pawn->count;
	deaths_min = ((pawn->hurt > hurt_withstand) ? (pawn->hurt - hurt_withstand) : 0);

	// Check if the minimum number of deaths is enough to kill all troops.
	if (deaths_min > pawn->count)
		return pawn->count;

	// Attackers tend to attack vulnerable targets which ensures a minimum number of deaths.
	deaths_certain = deaths_max / 2;
	if (deaths_min < deaths_certain) deaths_min = deaths_certain;

	assert(deaths_max >= deaths_min);
	deaths_actual = deaths_min + random() % (deaths_max - deaths_min + 1);
	return ((deaths_actual > pawn->count) ? pawn->count : deaths_actual);
}

// Determine whether the target obstacle is close enough to be attacked.
int can_assault(const struct position position, const struct battlefield *restrict target)
{
	float tile_left = ((target->blockage_location & POSITION_LEFT) ? target->tile.x : target->tile.x + WALL_OFFSET);
	float tile_right = ((target->blockage_location & POSITION_RIGHT) ? target->tile.x + 1 : target->tile.x + 1 - WALL_OFFSET);
	float tile_top = ((target->blockage_location & POSITION_TOP) ? target->tile.y : target->tile.y + WALL_OFFSET);
	float tile_bottom = ((target->blockage_location & POSITION_BOTTOM) ? target->tile.y + 1 : target->tile.y + 1 - WALL_OFFSET);

	float dx, dy;

	if (tile_left > position.x) dx = tile_left - position.x;
	else if (position.x > tile_right) dx = position.x - tile_right;
	else dx = 0;

	if (tile_top > position.y) dy = tile_top - position.y;
	else if (position.y > tile_bottom) dy = position.y - tile_bottom;
	else dy = 0;

	return (dx * dx + dy * dy <= DISTANCE_MELEE * DISTANCE_MELEE);
}

int can_fight(const struct position position, const struct pawn *restrict target)
{
	return (battlefield_distance(position, target->position) <= DISTANCE_MELEE);
}

static int can_shoot(const struct game *restrict game, const struct battle *battle, const struct pawn *pawn, const struct position target)
{
	// TODO loop only through the pawns in the area
	unsigned char shooter_alliance = game->players[pawn->troop->owner].alliance;
	for(size_t i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *other = battle->pawns + i;
		if (!other->count)
			continue;
		if ((game->players[other->troop->owner].alliance != shooter_alliance) && can_fight(other->position, pawn))
			return 0;
	}

	return 1;
}

void combat_melee(const struct game *restrict game, struct battle *restrict battle)
{
	for(size_t i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *fighter = battle->pawns + i;
		unsigned char fighter_alliance = game->players[fighter->troop->owner].alliance;

		if (!fighter->count) continue;
		if (fighter->action == ACTION_SHOOT) continue;

		if (fighter->action == ACTION_ASSAULT)
		{
			if (can_assault(fighter->position, fighter->target.field))
				fighter->target.field->strength -= (unsigned)combat_assault_damage(fighter, fighter->target.field);
		}
		else
		{
			struct pawn *victims[VICTIMS_LIMIT];
			unsigned victims_count = 0;
			unsigned victims_troops = 0;

			unsigned targets_attackers[VICTIMS_LIMIT];
			unsigned attackers_left = fighter->count;

			// Fight all enemy pawns nearby.
			for(size_t j = 0; j < battle->pawns_count; ++j)
			{
				struct pawn *victim = battle->pawns + j;
				if (!victim->count)
					continue;
				if (game->players[victim->troop->owner].alliance == fighter_alliance)
					continue;
				if (can_fight(fighter->position, victim))
				{
					assert(victims_count < VICTIMS_LIMIT);
					victims[victims_count++] = victim;
					victims_troops += victim->count;
				}
			}

			if (!victims_count)
				continue;

			// Distribute the attacking troops proportionally between victim troops.
			// Choose targets for the remainder randomly.
			for(size_t i = 0; i < victims_count; ++i)
			{
				// The number of troops, truncated to integer, is smaller than the mathematical value of the expression.
				// This guarantees that attackers_left cannot become negative.
				targets_attackers[i] = fighter->count * victims[i]->count / victims_troops;
				attackers_left -= targets_attackers[i];
			}
			while (attackers_left--)
				targets_attackers[random() % victims_count] += 1;

			for(size_t i = 0; i < victims_count; ++i)
			{
				unsigned fight_troops = combat_fight_troops(fighter, targets_attackers[i], victims[i]->count);
				victims[i]->hurt += (unsigned)combat_fight_damage(fighter, fight_troops, victims[i]);
				victims[i]->attackers += fight_troops;
			}
		}
	}
}

void combat_ranged(struct battle *restrict battle, const struct obstacles *restrict obstacles)
{
	for(size_t i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *shooter = battle->pawns + i;

		double inaccuracy;
		struct victim_shoot victims[VICTIMS_LIMIT];
		unsigned victims_count;

		if (!shooter->count)
			continue;
		if (shooter->action != ACTION_SHOOT)
			continue;

		inaccuracy = combat_shoot_inaccuracy(shooter, obstacles);

		victims_count = combat_shoot_victims(battle, shooter, victims);
		for(size_t j = 0; j < victims_count; ++j)
		{
			// Assume all troops can shoot this victim.
			victims[j].pawn->hurt += (unsigned)combat_shoot_damage(shooter, inaccuracy, victims[j].distance, victims[j].pawn);
			victims[j].pawn->attackers += shooter->count;
		}
	}
}

int combat_fight(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct pawn *restrict victim)
{
	// TODO verify that the target is reachable

	if (allies(game, fighter->troop->owner, victim->troop->owner))
		return 0;

	fighter->action = ACTION_FIGHT;
	fighter->target.pawn = victim;

	return 1;
}

int combat_assault(const struct game *restrict game, struct pawn *restrict fighter, struct battlefield *restrict target)
{
	// TODO verify that the target is reachable

	if ((target->blockage != BLOCKAGE_WALL) && (target->blockage != BLOCKAGE_GATE))
		return 0;

	// TODO should this check be here?
	if (!can_assault((fighter->path.count ? fighter->path.data[fighter->path.count - 1] : fighter->position), target))
		return 0;

	fighter->action = ACTION_ASSAULT;
	fighter->target.field = target;

	return 1;
}

int combat_shoot(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict shooter, struct position target)
{
	unsigned range;

	// Only ranged units can shoot.
	if (!shooter->troop->unit->ranged.weapon) return 0;

	// Don't allow sooting if there is an enemy pawn nearby.
	if (!can_shoot(game, battle, shooter, target))
		return 0;

	// Check if the target is in shooting range.
	// If there is an obstacle between the pawn and its target, decrease shooting range by 1.
	range = shooter->troop->unit->ranged.range;
	if (!path_visible(shooter->position, target, obstacles))
		range -= 1;
	if (battlefield_distance(shooter->position, target) > range)
		return 0;

	pawn_stay(shooter);
	shooter->action = ACTION_SHOOT;
	shooter->target.position = target;

	return 1;
}

int battlefield_clean(const struct game *restrict game, struct battle *restrict battle)
{
	size_t p;
	size_t x, y;

	int activity = 0;

	// Remove destroyed obstacles.
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
		for(x = 0; x < BATTLEFIELD_HEIGHT; ++x)
		{
			struct battlefield *restrict field = &battle->field[y][x];
			if (((field->blockage == BLOCKAGE_WALL) || (field->blockage == BLOCKAGE_GATE)) && !field->strength)
			{
				activity = 1;
				field->blockage = BLOCKAGE_NONE;
			}
		}

	// Calculate casualties and remove dead pawns.
	for(p = 0; p < battle->pawns_count; ++p)
	{
		struct pawn *pawn = battle->pawns + p;
		unsigned dead;

		if (!pawn->count) continue;

		if (dead = deaths(pawn))
		{
			activity = 1;

			pawn->hurt -= dead * pawn->troop->unit->health;
			pawn->count -= dead;
		}

		pawn->attackers = 0;
	}

	// Reset pawn action if the target is no longer valid.
	for(p = 0; p < battle->pawns_count; ++p)
	{
		struct pawn *pawn = battle->pawns + p;
		if (!pawn->count) continue;

		// Stop pawns from attacking dead pawns and destroyed obstacles.
		if ((pawn->action == ACTION_SHOOT) && !can_shoot(game, battle, pawn, pawn->target.position))
			pawn->action = 0;
		if ((pawn->action == ACTION_FIGHT) && !pawn->target.pawn->count)
			pawn->action = 0;
		if ((pawn->action == ACTION_ASSAULT) && !pawn->target.field->blockage)
			pawn->action = 0;

		// TODO what if the target pawn is no longer reachable?
	}

	return activity;
}
