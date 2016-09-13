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

// Instead of defining DISTANCE_MELEE directly, define its reciprocal to work-around C language limitation.
// http://stackoverflow.com/questions/38054023/c-fixed-size-array-treated-as-variable-size
// TODO try to fix this without the work-around
#define STEPS_FIELD 4
#define DISTANCE_MELEE ((PAWN_RADIUS * 2) + (1.0 / STEPS_FIELD)) /* 1.25 */
#define DISTANCE_RANGED (PAWN_RADIUS * 2) /* 1.0 */

// A pawn can have at most 7 pawns as victims.
// melee: 7 pawns placed at distance <= DISTANCE_MELEE from the attacker
// ranged: 1 pawn placed at the target and 6 pawns placed at distance <= DISTANCE_RANGED from it
#define VICTIMS_LIMIT 7

struct victim_shoot
{
	struct pawn *restrict pawn;
	double distance;
};

unsigned combat_fight_troops(const struct pawn *restrict fighter, unsigned fighter_troops, unsigned victim_troops);
double combat_fight_damage(const struct pawn *restrict fighter, unsigned fighter_troops, const struct pawn *restrict victim);

double combat_assault_distance(const struct position position, const struct obstacle *restrict obstacle);
double combat_assault_damage(const struct pawn *restrict fighter, const struct battlefield *restrict target);

unsigned combat_shoot_victims(struct battle *restrict battle, const struct pawn *restrict shooter, struct victim_shoot victims[static VICTIMS_LIMIT]);
double combat_shoot_inaccuracy(const struct pawn *restrict shooter, const struct obstacles *restrict obstacles);
double combat_shoot_damage(const struct pawn *restrict shooter, double inaccuracy, double distance_victim, const struct pawn *restrict victim);

int can_fight(const struct position position, const struct pawn *restrict pawn);
int can_assault(const struct position position, const struct battlefield *restrict field);

void combat_melee(const struct game *restrict game, struct battle *restrict battle);
void combat_ranged(struct battle *battle, const struct obstacles *restrict obstacles);
int battlefield_clean(const struct game *restrict game, struct battle *restrict battle);

int combat_fight(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict fighter, struct pawn *restrict victim);
int combat_assault(const struct game *restrict game, struct pawn *restrict fighter, struct battlefield *restrict target);
int combat_shoot(const struct game *restrict game, const struct battle *restrict battle, const struct obstacles *restrict obstacles, struct pawn *restrict shooter, struct position target);

extern const double damage_boost[7][6];
