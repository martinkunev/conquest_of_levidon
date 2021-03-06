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

#define REGIONS_LIMIT 256

#define PLAYER_NEUTRAL 0 /* player 0 is hard-coded as neutral */

// TODO don't use struct point here

#define region_built(region, building) ((_Bool)((region)->built & (1 << (building))))

#define COUNT_ROUND_PRECISION 10
#define count_round(count) (((count) + (COUNT_ROUND_PRECISION / 2)) / COUNT_ROUND_PRECISION)

enum {UnitPeasant, UnitMilitia, UnitPikeman, UnitArcher, UnitLongbow, UnitLightCavalry, UnitBatteringRam};

enum {BuildingFarm, BuildingIrrigation, BuildingSawmill, BuildingMine, BuildingBloomery, BuildingBarracks, BuildingArcheryRange, BuildingStables, BuildingWatchTower, BuildingPalisade, BuildingFortress, BuildingWorkshop, BuildingForge};

struct building
{
	char name[NAME_LIMIT];
	size_t name_length;

	struct resources cost, support, income;
	//unsigned workers;
	unsigned char time;
	uint32_t requires, conflicts;
};

struct troop
{
	struct troop *_prev, *_next;
	const struct unit *unit;
	unsigned count;
	unsigned char owner;

	// WARNING: Player-specific input variables below.

	unsigned char dismiss;

	struct region *location, *move;
};

#define LOCATION_GARRISON ((struct region *)0)

struct region
{
	char name[NAME_LIMIT];
	size_t name_length;

	size_t index;
	struct region *neighbors[NEIGHBORS_LIMIT];
	struct polygon *location;
	struct point location_garrison, center;

	struct troop *troops;

	unsigned char owner;

	unsigned char train_progress;
	unsigned char build_progress;
	uint32_t built;

	struct
	{
		// TODO implement this: const struct garrison_info *restrict info;
		unsigned char owner;
		unsigned siege;

		_Bool reinforce; // WARNING: player-specific input variable
	} garrison;

	// WARNING: Player-specific input variables below.

	const struct unit *train[TRAIN_QUEUE];

	unsigned population;
	struct
	{
		// Values are in hundreths (when used, they are multiplied by 0.01).
		unsigned food;
		unsigned wood;
		unsigned iron;
		unsigned stone;
	} workers;

	signed char construct; // -1 == no construction
};

enum {PALISADE, FORTRESS, /* dummy */ GARRISONS_COUNT};
struct garrison_info
{
	size_t index;

	unsigned troops;
	unsigned provisions;
	struct unit wall, gate;
};

static inline const struct garrison_info *garrison_info(const struct region *restrict region)
{
	static const struct garrison_info info[] =
	{
		[PALISADE] = {
			.index = PALISADE, .troops = 3, .provisions = 2,
			.wall = {.health = 160, .armor = ARMOR_WOODEN},
			.gate = {.health = 80, .armor = ARMOR_WOODEN},
		},
		[FORTRESS] = {
			.index = FORTRESS, .troops = 6, .provisions = 5,
			.wall = {.health = 200, .armor = ARMOR_STONE},
			.gate = {.health = 120, .armor = ARMOR_WOODEN},
		},
	};

	if (region_built(region, BuildingFortress)) return info + FORTRESS;
	else if (region_built(region, BuildingPalisade)) return info + PALISADE;
	else return 0;
}

#define UNITS_COUNT 7
extern const struct unit UNITS[UNITS_COUNT];

extern const struct building BUILDINGS[];
extern const size_t BUILDINGS_COUNT;

#define region_unit_available(region, unit) (!((unit).requires & ~(region)->built))

static inline int region_building_available(const struct region *restrict region, const struct building *restrict building)
{
	return ((region->built & (building->requires | building->conflicts)) == building->requires);
}

static inline unsigned workers_population(const struct region *restrict region, unsigned workers)
{
	return (unsigned)(region->population * (workers / 100.0));
}

void troop_attach(struct troop **troops, struct troop *troop);
void troop_detach(struct troop **troops, struct troop *troop);
void troop_remove(struct troop **troops, struct troop *troop);

int troop_spawn(struct region *restrict region, struct troop **restrict troops, const struct unit *restrict unit, unsigned count, unsigned char owner);

void region_income(const struct region *restrict region, unsigned char player, struct resources *restrict income);
void region_production(const struct region* restrict region, struct resources *restrict income);

void region_battle_cleanup(const struct game *restrict game, struct region *restrict region, int assault, unsigned winner_alliance);
void region_turn_process(const struct game *restrict game, struct region *restrict region);

void region_orders_process(struct region *restrict region);
void region_orders_cancel(struct region *restrict region);

int polygons_border(const struct polygon *restrict a, const struct polygon *restrict b, struct point *restrict first, struct point *restrict second);

void map_visible(const struct game *restrict game, unsigned char player, unsigned char visible[REGIONS_LIMIT]);

int region_garrison_full(const struct region *restrict region, const struct garrison_info *restrict garrison);
void region_troops_merge(struct region *restrict region);
