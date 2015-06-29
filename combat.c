#include <stdlib.h>

#include "types.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "movement.h"
#include "combat.h"

static const double damage_boost[6][6] =
{
// ARMOR:					NONE		LEATHER		CHAINMAIL	PLATE		WOODEN		STONE
	[WEAPON_NONE] =	{		0.0,		0.0,		0.0,		0.0,		0.0,		0.0},
	[WEAPON_ARROW] = {		1.0,		0.8,		0.7,		0.2,		0.0,		0.0},
	[WEAPON_CLEAVING] = {	1.0,		0.8,		0.7,		0.3,		0.5,		0.0},
	[WEAPON_POLEARM] = {	1.0,		1.0,		0.8,		0.4,		0.2,		0.0},
	[WEAPON_BLADE] = {		1.0,		1.0,		0.9,		0.5,		0.2,		0.0},
	[WEAPON_BLUNT] = {		1.0,		1.0,		1.0,		0.6,		1.0,		0.7},
};

// Deals damage to a pawn.
static void pawn_deal(struct pawn *pawn, unsigned damage)
{
	if (!pawn) return;
	pawn->hurt += damage;
}

// TODO this should not be here
// TODO deprecated
static int battlefield_fightable(const struct pawn *restrict pawn, const struct pawn *restrict target, const struct battle *restrict battle)
{
	if (!target || !target->troop->count) return 0;

	struct point position = pawn->moves[0].location;
	struct point target_position = target->moves[0].location;

	// TODO what if one of the pawns is on a tower

	// TODO return 0 if the pawns are in the same alliance

	return battlefield_neighbors(pawn->moves[0].location, target->moves[0].location);
}

void battlefield_fight(const struct game *restrict game, struct battle *restrict battle) // TODO rename to something like combat_melee
{
	size_t i, j;
	for(i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *fighter = battle->pawns + i;
		unsigned char fighter_alliance = game->players[fighter->troop->owner].alliance;

		if (!fighter->troop->count) continue;
		if (fighter->action == PAWN_SHOOT) continue;

		unsigned damage_total = fighter->troop->unit->melee.damage * fighter->troop->count;

		if (fighter->action == PAWN_ASSAULT)
		{
			struct battlefield *restrict field = &battle->field[fighter->target.field.y][fighter->target.field.x];
			if (damage_total >= field->strength) field->strength = 0;
			else field->strength -= damage_total;
		}
		else // fighter->action == PAWN_FIGHT
		{
			struct pawn *victims[4], *victim;
			unsigned victims_count = 0;

			// If the pawn has a specific fight target and is able to fight it, fight only that target.
			if ((fighter->action == PAWN_FIGHT) && battlefield_fightable(fighter, fighter->target.pawn, battle))
			{
				victims[0] = fighter->target.pawn;
				victims_count = 1;
			}
			else
			{
				int x = fighter->moves[0].location.x;
				int y = fighter->moves[0].location.y;

				// Look for pawns to fight at the neighboring fields.
				if ((x > 0) && (victim = battle->field[y][x - 1].pawn) && (game->players[victim->troop->owner].alliance != fighter_alliance))
					victims[victims_count++] = victim;
				if ((x < (BATTLEFIELD_WIDTH - 1)) && (victim = battle->field[y][x + 1].pawn) && (game->players[victim->troop->owner].alliance != fighter_alliance))
					victims[victims_count++] = victim;
				if ((y > 0) && (victim = battle->field[y - 1][x].pawn) && (game->players[victim->troop->owner].alliance != fighter_alliance))
					victims[victims_count++] = victim;
				if ((y < (BATTLEFIELD_HEIGHT - 1)) && (victim = battle->field[y + 1][x].pawn) && (game->players[victim->troop->owner].alliance != fighter_alliance))
					victims[victims_count++] = victim;
				if (!victims_count) continue; // nothing to fight
			}

			unsigned damage = ((double)damage_total / victims_count + 0.5);
			for(j = 0; j < victims_count; ++j)
				pawn_deal(victims[j], damage);
		}
	}
}

void battlefield_shoot(struct battle *battle)
{
	const double targets[3][2] = {{1, 0.5}, {0, 0.078125}, {0, 0.046875}}; // 1/2, 5/64, 3/64

	size_t i;
	for(i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *shooter = battle->pawns + i;

		if (!shooter->troop->count) continue;
		if (shooter->action != PAWN_SHOOT) continue;

		unsigned damage_total = shooter->troop->unit->ranged.damage * shooter->troop->count;
		unsigned damage;

		unsigned target_index;
		double distance, miss, on_target;

		int x = shooter->target.field.x;
		int y = shooter->target.field.y;
		struct pawn *victim = battle->field[y][x].pawn;

		distance = battlefield_distance(shooter->moves[0].location, shooter->target.field);
		miss = distance / shooter->troop->unit->ranged.range;

		// Shooters have some chance to hit a field adjacent to the target, depending on the distance.
		// Damage is dealt to the target field and to its neighbors.

		target_index = 0;
		on_target = targets[target_index][0] * (1 - miss) + targets[target_index][1] * miss;
		pawn_deal(battle->field[y][x].pawn, (unsigned)(damage * on_target + 0.5));

		target_index = 1;
		on_target = targets[target_index][0] * (1 - miss) + targets[target_index][1] * miss;
		damage = (unsigned)(damage_total * on_target + 0.5);
		if (x > 0) pawn_deal(battle->field[y][x - 1].pawn, damage);
		if (x < (BATTLEFIELD_WIDTH - 1)) pawn_deal(battle->field[y][x + 1].pawn, damage);
		if (y > 0) pawn_deal(battle->field[y - 1][x].pawn, damage);
		if (y < (BATTLEFIELD_HEIGHT - 1)) pawn_deal(battle->field[y + 1][x].pawn, damage);

		target_index = 2;
		on_target = targets[target_index][0] * (1 - miss) + targets[target_index][1] * miss;
		damage = (unsigned)(damage_total * on_target + 0.5);
		if (x > 0)
		{
			if (y > 0) pawn_deal(battle->field[y - 1][x - 1].pawn, damage);
			if (y < (BATTLEFIELD_HEIGHT - 1)) pawn_deal(battle->field[y + 1][x - 1].pawn, damage);
		}
		if (x < (BATTLEFIELD_WIDTH - 1))
		{
			if (y > 0) pawn_deal(battle->field[y - 1][x + 1].pawn, damage);
			if (y < (BATTLEFIELD_HEIGHT - 1)) pawn_deal(battle->field[y + 1][x + 1].pawn, damage);
		}

		// TODO ?deal more damage to moving pawns
	}
}

// Determine how many units to kill.
static unsigned pawn_victims(unsigned min, unsigned max)
{
	// The possible outcomes are all the integers in [min, max].
	if (max == min) return min;
	unsigned outcomes = (max - min + 1);

	// TODO ?use a better algorithm here
	// For the outcomes min and max there is 1 chance value (least probable).
	// Outcomes closer to the middle of the interval are more probable than the ones farther from it.
	// When going from the end of the interval to the middle, the number of chance values increases by 2 with every integer.
	// Example: for the interval [2, 6], the chance values are: 1, 3, 5, 3, 1

	unsigned chances, chance;
	chances = 2 * (outcomes / 2) * (outcomes / 2);
	if (outcomes % 2) chances += outcomes;
	chance = random() % chances;

	// Find the outcome corresponding to the chosen chance value.
	size_t distance = 0;
	if (chance < chances / 2)
	{
		// left half of the interval [min, max]
		while ((distance + 1) * (distance + 1) <= chance) distance += 1;
		return min + distance;
	}
	else
	{
		// right half of the interval [min, max]
		while ((distance + 1) * (distance + 1) <= (chances - chance)) distance += 1;
		return max - distance;
	}
}

void battlefield_clean_corpses(struct battle *battle)
{
	size_t p;
	struct pawn *pawn;
	struct troop *troop;
	for(p = 0; p < battle->pawns_count; ++p)
	{
		pawn = battle->pawns + p;
		troop = pawn->troop;

		if (!troop->count) continue;

		if ((troop->count * troop->unit->health) <= pawn->hurt)
		{
			// All units in this pawn are killed.
			troop->count = 0;
			battle->field[pawn->moves[0].location.y][pawn->moves[0].location.x].pawn = 0;
		}
		else
		{
			// Find the minimum and maximum of units that can be killed.
			unsigned max = pawn->hurt / troop->unit->health;
			unsigned min;
			if ((troop->unit->health - 1) * troop->count >= pawn->hurt) min = 0;
			else min = pawn->hurt % troop->count;

			unsigned victims = pawn_victims(min, max);
			troop->count -= victims;
			pawn->hurt -= victims * troop->unit->health;
		}
	}
}

void battlefield_clean_ruines(struct battle *battle)
{
	size_t x, y;

	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
		for(x = 0; x < BATTLEFIELD_HEIGHT; ++x)
		{
			struct battlefield *restrict field = &battle->field[y][x];
			if ((field->blockage == BLOCKAGE_OBSTACLE) && !field->strength)
				field->blockage = BLOCKAGE_OBSTACLE;
		}
}

int combat_fight(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct pawn *restrict victim)
{
	// TODO what if one of the pawns is on a tower

	if (allies(game, fighter->troop->owner, victim->troop->owner))
		return 0;

	struct point fighter_field = fighter->moves[fighter->moves_count - 1].location;
	struct point victim_field = victim->moves[0].location;
	if (!battlefield_neighbors(fighter_field, victim_field))
		return 0;

	fighter->action = PAWN_FIGHT;
	fighter->target.pawn = victim;

	return 1;
}

int combat_assault(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct point target)
{
	// TODO what if the fighter attacks a tower with pawn on it

	const struct battlefield *restrict field = &battle->field[target.y][target.x];

	if ((field->blockage != BLOCKAGE_OBSTACLE) || allies(game, fighter->troop->owner, field->owner))
		return 0;

	struct point fighter_field = fighter->moves[fighter->moves_count - 1].location;
	if (!battlefield_neighbors(fighter_field, target))
		return 0;

	fighter->action = PAWN_ASSAULT;
	fighter->target.field = target;

	return 1;
}

int combat_shoot(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict shooter, struct point target)
{
	// Only ranged units can shoot.
	if (!shooter->troop->unit->ranged.weapon) return 0;

	struct point shooter_field = shooter->moves[0].location;

	// Don't allow sooting if there is a neighbor enemy pawn.
	{
		int x = shooter_field.x;
		int y = shooter_field.y;

		struct pawn *neighbor;

		if ((x > 0) && (neighbor = battle->field[y][x - 1].pawn) && !allies(game, shooter->troop->owner, neighbor->troop->owner))
			return 0;
		if ((x < (BATTLEFIELD_WIDTH - 1)) && (neighbor = battle->field[y][x + 1].pawn) && !allies(game, shooter->troop->owner, neighbor->troop->owner))
			return 0;
		if ((y > 0) && (neighbor = battle->field[y - 1][x].pawn) && !allies(game, shooter->troop->owner, neighbor->troop->owner))
			return 0;
		if ((y < (BATTLEFIELD_HEIGHT - 1)) && (neighbor = battle->field[y + 1][x].pawn) && !allies(game, shooter->troop->owner, neighbor->troop->owner))
			return 0;
	}

	// Check if the target is in shooting range.
	{
		unsigned range = shooter->troop->unit->ranged.range;

		// If there is an obstacle between the pawn and its target, decrease shooting range by 1.
		// TODO what if there is a tower in one of the fields
		if (!path_visible(shooter_field, target, obstacles))
			range -= 1;

		unsigned distance = round(battlefield_distance(shooter_field, target));
		if (distance > range)
			return 0;
	}

	movement_stay(shooter);
	shooter->action = PAWN_SHOOT;
	shooter->target.field = target;

	return 1;
}
