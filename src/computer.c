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

// WARNING: The AI logic assumes that troops have only negative income (used in expense_significance() and map_state_rating()).

#include <stdint.h>
#include <stdlib.h>

#include "game.h"
#include "draw.h"
#include "map.h"
#include "pathfinding.h"
#include "movement.h"
#include "battle.h"
#include "combat.h"

const double desire_buildings[] =
{
	[BuildingFarm] = 1.0,
	[BuildingIrrigation] = 0.5,
	[BuildingSawmill] = 1.0,
	[BuildingMine] = 0.8,
	[BuildingBloomery] = 0.6,
	[BuildingWatchTower] = 0.1,
	[BuildingPalisade] = 0.3,
	[BuildingFortress] = 0.2,
/*	[BuildingBarracks] = 0.75,
	[BuildingArcheryRange] = 0.75,
	[BuildingStables] = 0.4,
	[BuildingWorkshop] = 0.4,
	[BuildingForge] = 0.4,*/
};

// Returns how significant is an expense.
double expense_significance(const struct resources *restrict expense)
{
	// TODO more sophisticated logic here
	double value = 1.0 - (expense->food * 1.0 + expense->wood * 1.5 + expense->stone * 1.5 + expense->gold * 1.5 + expense->iron * 2.0);
	return sqrt(value);
}

// Returns how valuable is the unit by its skills.
double unit_importance(const struct unit *restrict unit, const struct garrison_info *restrict garrison)
{
	// TODO more sophisticated logic here
	// TODO importance should depend on battle obstacles (e.g. the more they are, the more important is battering ram); if the battering ram is owned by the defender, it is not that important

	double importance;

	if (garrison)
	{
		importance = unit->health / damage_boost[WEAPON_CLEAVING][unit->armor] + (unit->speed - 2) * 2;
		importance += unit->melee.damage * damage_boost[unit->melee.weapon][ARMOR_LEATHER] * unit->melee.agility * 3;
		importance += unit->melee.damage * damage_boost[unit->melee.weapon][garrison->gate.armor] * 5;
		importance += unit->ranged.damage * (unit->ranged.range - 1);
	}
	else
	{
		importance = unit->health / damage_boost[WEAPON_CLEAVING][unit->armor] + (unit->speed - 2) * 2;
		importance += unit->melee.damage * damage_boost[unit->melee.weapon][ARMOR_LEATHER] * unit->melee.agility * 3;
		importance += unit->melee.damage * damage_boost[unit->melee.weapon][ARMOR_WOODEN] * 2;
		importance += unit->melee.damage * damage_boost[unit->melee.weapon][ARMOR_STONE] * 2;
		importance += unit->ranged.damage * unit->ranged.range;
	}

	return importance;
}

int state_wanted(double rate, double rate_new, double temperature)
{
	// temperature is in [0, 1]
	// When the temperature is 0, only states with higher rate can be accepted.

	double probability_preserve = exp(rate_new - rate - temperature);
	return probability_preserve < ((double)random() / RAND_MAX);
}

#if defined(UNIT_IMPORTANCE)
#include <stdio.h>

int main(void)
{
	printf("%16s %8s %12s %16s %16s\n", "name", "count", "importance", "total importance", "usefulness");
	for(size_t i = 0; i < UNITS_COUNT; ++i)
	{
		double importance = unit_importance(UNITS + i, 0);
		printf("%16.*s %8u %12f %16f %16f\n", (int)UNITS[i].name_length, UNITS[i].name, (unsigned)UNITS[i].troops_count, importance, importance * UNITS[i].troops_count, importance * UNITS[i].troops_count / expense_significance(&UNITS[i].cost));
	}
	return 0;
}
#endif
