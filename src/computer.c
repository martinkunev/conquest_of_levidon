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

const double desire_buildings[] =
{
	[BuildingFarm] = 0.9,
	[BuildingIrrigation] = 0.5,
	[BuildingSawmill] = 1.0,
	[BuildingMine] = 0.8,
	[BuildingBloomery] = 0.5,
	[BuildingBarracks] = 0.8,
	[BuildingArcheryRange] = 0.75,
	[BuildingStables] = 0.4,
	[BuildingWatchTower] = 0.5,
	[BuildingPalisade] = 0.6,
	[BuildingFortress] = 0.4,
	[BuildingWorkshop] = 0.3,
	[BuildingForge] = 0.5,
};

double unit_importance(const struct unit *restrict unit, const struct garrison_info *restrict garrison)
{
	// TODO more sophisticated logic here
	// TODO importance should depend on battle obstacles (e.g. the more they are, the more important is battering ram); if the battering ram is owned by the defender, it is not that important

	double importance;

	if (garrison)
	{
		importance = unit->health / damage_boost[WEAPON_CLEAVING][unit->armor] + (unit->speed - 1) * 1.5;
		importance += unit->melee.damage * damage_boost[unit->melee.weapon][ARMOR_LEATHER] * unit->melee.agility * 3;
		importance += unit->melee.damage * damage_boost[unit->melee.weapon][garrison->gate.armor] * 5;
		importance += unit->ranged.damage * (unit->ranged.range - 1);
	}
	else
	{
		importance = unit->health / damage_boost[WEAPON_CLEAVING][unit->armor] + unit->speed * 1.5;
		importance += unit->melee.damage * damage_boost[unit->melee.weapon][ARMOR_LEATHER] * unit->melee.agility * 3;
		importance += unit->melee.damage * damage_boost[unit->melee.weapon][ARMOR_WOODEN] * 2;
		importance += unit->melee.damage * damage_boost[unit->melee.weapon][ARMOR_STONE] * 2;
		importance += unit->ranged.damage * unit->ranged.range;
	}

	return importance;
}

double unit_cost(const struct unit *restrict unit)
{
	return -125.0 * (unit->cost.food * 2.0 + unit->cost.wood * 2.0 + unit->cost.stone * 2.0 + unit->cost.gold * 2.5 + unit->cost.iron * 4.0);
}

int state_wanted(double rate, double rate_new, double temperature)
{
	// temperature is in [0, 1]

	double probability_preserve = exp(rate_new - rate - temperature);
	return probability_preserve < ((double)random() / RAND_MAX);

	// e ^ ((rate_new - rate) / temperature)

	//double probability = exp((rate_new - rate) / temperature);
	//return probability > ((double)random() / RAND_MAX);
}

#if defined(UNIT_IMPORTANCE)
#include <stdio.h>

int main(void)
{
	printf("%16s %12s %8s %20s\n", "name", "importance", "count", "total importance");
	for(size_t i = 0; i < UNITS_COUNT; ++i)
	{
		double importance = unit_importance(UNITS + i, 0);
		printf("%16.*s %12f %8u %20f\n", (int)UNITS[i].name_length, UNITS[i].name, importance, (unsigned)UNITS[i].troops_count, importance * UNITS[i].troops_count);
	}
	return 0;
}
#endif
