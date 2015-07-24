#include <stdlib.h>

#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "movement.h"
#include "combat.h"

#define ACCURACY 0.5

#define MISS_MAX 20 /* 20% */

static const double damage_boost[7][6] =
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

	// The attacking troops can sometimes miss.
	damage *= (100 - random() % (MISS_MAX + 1)) / 100.0;

	// Each attacker deals damage to a single troop.
	// The attacker cannot deal more damage than the health of the troop.
	if (damage > unit->health) damage = unit->health;

	unsigned damage_total = (unsigned)(damage * attacker_troops + 0.5);

	unsigned defender_troops = victim->count;
	if (defender_troops > attacker_troops) defender_troops = attacker_troops;

	unsigned dead = deaths(damage_total, defender_troops, unit->health);

	victim->dead += dead;
	victim->hurt += damage_total - (dead * unit->health);
}

static void damage_fight(const struct pawn *restrict fighter, struct pawn *restrict victims[], size_t victims_count)
{
	size_t i;

	enum weapon weapon = fighter->troop->unit->melee.weapon;
	double damage = fighter->troop->unit->melee.damage * fighter->troop->unit->melee.agility;

	// assert(victims_count <= 4);

	// Distribute the attacking troops equally between the victims.
	// Choose targets for the remainder randomly.
	unsigned attackers = fighter->count / victims_count;
	unsigned targets_attackers[] = {attackers, attackers, attackers, attackers};
	unsigned left = fighter->count % victims_count;
	while (left--) targets_attackers[random() % victims_count] += 1;

	for(i = 0; i < victims_count; ++i)
		damage_deal(damage * damage_boost[weapon][victims[i]->troop->unit->armor], targets_attackers[i], victims[i]);
}

static void damage_assault(struct battle *restrict battle, const struct pawn *restrict fighter, struct point target)
{
	struct battlefield *restrict field = &battle->field[target.y][target.x];
	enum armor armor = field->armor;

	enum weapon weapon = fighter->troop->unit->melee.weapon;
	unsigned damage = (unsigned)(fighter->troop->unit->melee.damage * fighter->count * damage_boost[weapon][armor] + 0.5);

	if (damage >= field->strength) field->strength = 0;
	else field->strength -= damage;
}

static void damage_shoot(struct pawn *shooter, struct pawn *victim, double damage)
{
	if (!victim) return;

	// Assume all troops can shoot this victim.
	damage_deal(damage * damage_boost[shooter->troop->unit->ranged.weapon][victim->troop->unit->armor], shooter->count, victim);
}

void battlefield_fight(const struct game *restrict game, struct battle *restrict battle) // TODO rename to something like combat_melee
{
	size_t i;
	for(i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *fighter = battle->pawns + i;
		unsigned char fighter_alliance = game->players[fighter->troop->owner].alliance;

		if (!fighter->count) continue;
		if (fighter->action == PAWN_SHOOT) continue;

		if (fighter->action == PAWN_ASSAULT)
		{
			if (battlefield_neighbors(fighter->moves[0].location, fighter->target.field))
				damage_assault(battle, fighter, fighter->target.field);
		}
		else // fighter->action == PAWN_FIGHT
		{
			struct pawn *victims[4], *victim;
			unsigned victims_count = 0;

			// TODO what if one of the pawns is on a tower

			// If the pawn has a specific fight target and is able to fight it, fight only that target.
			if ((fighter->action == PAWN_FIGHT) && battlefield_neighbors(fighter->moves[0].location, fighter->target.pawn->moves[0].location))
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

			damage_fight(fighter, victims, victims_count);
		}
	}
}

void battlefield_shoot(struct battle *battle, const struct obstacles *restrict obstacles)
{
	const double targets[3][2] = {{1, 0.5}, {0, 0.078125}, {0, 0.046875}}; // 1/2, 5/64, 3/64

	size_t i;
	for(i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *shooter = battle->pawns + i;

		if (!shooter->count) continue;
		if (shooter->action != PAWN_SHOOT) continue;

		unsigned target_index;
		double miss, on_target;
		unsigned range;

		int x = shooter->target.field.x;
		int y = shooter->target.field.y;

		double damage_total = shooter->troop->unit->ranged.damage;
		unsigned damage;

		// TODO what if there is a tower on one of the fields

		range = shooter->troop->unit->ranged.range;
		if (!target_visible(shooter->moves[0].location, shooter->target.field, obstacles))
		{
			damage_total *= ACCURACY;
			range -= 1;
		}
		miss = battlefield_distance(shooter->moves[0].location, shooter->target.field) / range;

		// Shooters have some chance to hit a field adjacent to the target, depending on the distance.
		// Damage is dealt to the target field and to its neighbors.

		target_index = 0;
		on_target = targets[target_index][0] * (1 - miss) + targets[target_index][1] * miss;
		damage = (unsigned)(damage_total * on_target + 0.5);
		damage_shoot(shooter, battle->field[y][x].pawn, damage);

		target_index = 1;
		on_target = targets[target_index][0] * (1 - miss) + targets[target_index][1] * miss;
		damage = (unsigned)(damage_total * on_target + 0.5);
		if (x > 0) damage_shoot(shooter, battle->field[y][x - 1].pawn, damage);
		if (x < (BATTLEFIELD_WIDTH - 1)) damage_shoot(shooter, battle->field[y][x + 1].pawn, damage);
		if (y > 0) damage_shoot(shooter, battle->field[y - 1][x].pawn, damage);
		if (y < (BATTLEFIELD_HEIGHT - 1)) damage_shoot(shooter, battle->field[y + 1][x].pawn, damage);

		target_index = 2;
		on_target = targets[target_index][0] * (1 - miss) + targets[target_index][1] * miss;
		damage = (unsigned)(damage_total * on_target + 0.5);
		if (x > 0)
		{
			if (y > 0) damage_shoot(shooter, battle->field[y - 1][x - 1].pawn, damage);
			if (y < (BATTLEFIELD_HEIGHT - 1)) damage_shoot(shooter, battle->field[y + 1][x - 1].pawn, damage);
		}
		if (x < (BATTLEFIELD_WIDTH - 1))
		{
			if (y > 0) damage_shoot(shooter, battle->field[y - 1][x + 1].pawn, damage);
			if (y < (BATTLEFIELD_HEIGHT - 1)) damage_shoot(shooter, battle->field[y + 1][x + 1].pawn, damage);
		}
	}
}

void battlefield_clean(struct battle *battle)
{
	size_t p;
	size_t x, y;

	// Remove destroyed obstacles.
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
		for(x = 0; x < BATTLEFIELD_HEIGHT; ++x)
		{
			struct battlefield *restrict field = &battle->field[y][x];
			if ((field->blockage == BLOCKAGE_OBSTACLE) && !field->strength)
				field->blockage = BLOCKAGE_NONE;
		}

	// Remove dead pawns.
	for(p = 0; p < battle->pawns_count; ++p)
	{
		struct pawn *pawn = battle->pawns + p;
		unsigned health = pawn->troop->unit->health;

		if (!pawn->count) continue;

		unsigned dead = deaths(pawn->hurt, pawn->count, health);

		pawn->dead += dead;
		pawn->hurt -= dead * health;

		if (pawn->count <= pawn->dead)
		{
			// Remove dead pawns from the battlefield.
			battle->field[pawn->moves[0].location.y][pawn->moves[0].location.x].pawn = 0;
			pawn->count = 0;
		}
		else pawn->count -= pawn->dead;
		pawn->dead = 0;
	}

	// Stop pawns from attacking dead pawns and destroyed obstacles.
	for(p = 0; p < battle->pawns_count; ++p)
	{
		struct pawn *pawn = battle->pawns + p;

		if ((pawn->action == PAWN_FIGHT) && !pawn->target.pawn->count)
			pawn->action = 0;

		if ((pawn->action == PAWN_ASSAULT) && !battle->field[pawn->target.field.y][pawn->target.field.x].blockage)
			pawn->action = 0;
	}
}

int combat_order_fight(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct pawn *restrict victim)
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

int combat_order_assault(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct point target)
{
	// TODO what if the fighter attacks a tower with pawn on it

	const struct battlefield *restrict field = &battle->field[target.y][target.x];

	if ((field->blockage != BLOCKAGE_OBSTACLE) || allies(game, fighter->troop->owner, field->owner)) // TODO the allies check works only for gates
		return 0;

	struct point fighter_field = fighter->moves[fighter->moves_count - 1].location;
	if (!battlefield_neighbors(fighter_field, target))
		return 0;

	fighter->action = PAWN_ASSAULT;
	fighter->target.field = target;

	return 1;
}

int combat_order_shoot(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict shooter, struct point target)
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
		// TODO what if there is a tower on one of the fields
		if (!target_visible(shooter_field, target, obstacles))
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
