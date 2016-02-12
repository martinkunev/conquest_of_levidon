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

#include <stdlib.h>

#include "map.h"
#include "pathfinding.h"
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

double unit_importance(const struct unit *restrict unit)
{
	// TODO more sophisticated logic here
	// TODO importance should depend on battle obstacles (e.g. the more they are, the more important is battering ram)

	double importance = unit->health / damage_boost[WEAPON_CLEAVING][unit->armor] + unit->speed * 1.5;
	importance += unit->melee.damage * damage_boost[unit->melee.weapon][ARMOR_LEATHER] * unit->melee.agility * 3;
	importance += unit->melee.damage * damage_boost[unit->melee.weapon][ARMOR_WOODEN] * 2;
	importance += unit->melee.damage * damage_boost[unit->melee.weapon][ARMOR_STONE] * 2;
	importance += unit->ranged.damage * unit->ranged.range;
	return importance;
}

double unit_importance_assault(const struct unit *restrict unit, const struct garrison_info *restrict garrison)
{
	// TODO more sophisticated logic here
	// TODO importance should depend on battle obstacles (e.g. the more they are, the more important is battering ram)

	double importance = unit->health / damage_boost[WEAPON_CLEAVING][unit->armor] + (unit->speed - 1) * 1.5;
	importance += unit->melee.damage * damage_boost[unit->melee.weapon][ARMOR_LEATHER] * unit->melee.agility * 3;
	importance += unit->melee.damage * damage_boost[unit->melee.weapon][garrison->armor_gate] * 5;
	importance += unit->ranged.damage * (unit->ranged.range - 1);
	return importance;
}

double unit_cost(const struct unit *restrict unit)
{
	return 125.0 * (unit->cost.food * 2.0 + unit->cost.wood * 2.0 + unit->cost.stone * 2.0 + unit->cost.gold * 2.5 + unit->cost.iron * 4.0);
}

int state_wanted(double rate, double rate_new, double temperature)
{
	// e ^ ((rate_new - rate) / temperature)

	double probability = exp((rate_new - rate) / temperature);
	return probability > ((double)random() / RAND_MAX);
}
