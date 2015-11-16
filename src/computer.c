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
	[BuildingMine] = 0.7,
	[BuildingBlastFurnace] = 0.5,
	[BuildingBarracks] = 0.8,
	[BuildingArcheryRange] = 0.8,
	[BuildingStables] = 0.4,
	[BuildingWatchTower] = 0.5,
	[BuildingPalisade] = 0.6,
	[BuildingFortress] = 0.4,
	[BuildingWorkshop] = 0.3,
	[BuildingForge] = 0.5,
};

// TODO this should not be hard-coded
const double desire_units[] =
{
	[UnitPeasant] = 0.3,
	[UnitMilitia] = 0.5,
	[UnitPikeman] = 0.6,
	[UnitArcher] = 0.5,
	[UnitLongbow] = 0.6,
	[UnitLightCavalry] = 0.7,
	[UnitBatteringRam] = 0.4,
};

// unit				importance	count	cost
// Peasant:			7			25
// Militia:			9			25
// Pikeman:			10			25		8
// Archer:			8.5			25
// Longbow:			13			25
// Light cavalry:	14			16		7
// Battering ram:	72.5		1		9

double unit_importance(const struct unit *restrict unit)
{
	// TODO more sophisticated logic here
	// TODO importance should depend on battle obstacles (e.g. the more they are, the more important is battering ram)

	return unit->health + unit->melee.damage * unit->melee.agility * 2 + unit->ranged.damage * 3;
}

double unit_cost(const struct unit *restrict unit)
{
	return unit->cost.food + unit->cost.wood + unit->cost.stone + unit->cost.gold * 2 + unit->cost.iron * 3;
}

double unit_importance_assault(const struct unit *restrict unit, const struct garrison_info *restrict garrison)
{
	// TODO improve this?
	double importance = unit->health;
	importance += unit->melee.damage * damage_boost[unit->melee.weapon][garrison->armor_gate] * 2;
	importance += unit->ranged.damage * damage_boost[unit->ranged.weapon][garrison->armor_gate] * 3;
	return importance;
}

int state_wanted(double rate, double rate_new, double temperature)
{
	// e ^ ((rate_new - rate) / temperature)

	double probability = exp((rate_new - rate) / temperature);
	return probability > ((double)random() / RAND_MAX);
}
