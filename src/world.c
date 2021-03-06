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

#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "errors.h"
#include "json.h"
#include "log.h"
#include "draw.h"
#include "game.h"
#include "map.h"
#include "world.h"

// WARNING: Player 0 and alliance 0 are hard-coded as neutral.

#define NAME(string) .name = string, .name_length = sizeof(string) - 1

#define S(s) (s), sizeof(s) - 1

const struct unit UNITS[UNITS_COUNT] =
{
	[UnitPeasant] = {
		.index = UnitPeasant, NAME("Peasant"), .speed = 4, .health = 4, .armor = ARMOR_NONE,
		.cost = {.gold = -25}, .support = {.food = -1}, .time = 1, .troops_count = 25,
		.melee = {.weapon = WEAPON_CLUB, .damage = 1.0, .agility = 0.75},
	},
	[UnitMilitia] = {
		.index = UnitMilitia, NAME("Militia"), .speed = 5, .health = 5, .armor = ARMOR_LEATHER,
		.cost = {.gold = -30, .wood = -20}, .support = {.food = -1}, .time = 1, .troops_count = 25, .requires = (1 << BuildingBarracks),
		.melee = {.weapon = WEAPON_CLEAVING, .damage = 1.5, .agility = 1.0},
	},
	[UnitPikeman] = {
		.index = UnitPikeman, NAME("Pikeman"), .speed = 5, .health = 5, .armor = ARMOR_CHAINMAIL,
		.cost = {.gold = -40, .iron = -20}, .support = {.food = -1}, .time = 1, .troops_count = 25, .requires = (1 << BuildingForge),
		.melee = {.weapon = WEAPON_POLEARM, .damage = 2.0, .agility = 1.0},
	},
	[UnitArcher] = {
		.index = UnitArcher, NAME("Archer"), .speed = 4, .health = 4, .armor = ARMOR_NONE,
		.cost = {.gold = -30, .wood = -10}, .support = {.food = -1}, .time = 1, .troops_count = 25, .requires = (1 << BuildingArcheryRange),
		.melee = {.weapon = WEAPON_CLUB, .damage = 0.75, .agility = 0.75},
		.ranged = {.weapon = WEAPON_ARROW, .damage = 1.0, .range = 5},
	},
	[UnitLongbow] = {
		.index = UnitLongbow, NAME("Longbow"), .speed = 4, .health = 5, .armor = ARMOR_LEATHER,
		.cost = {.gold = -40, .wood = -40}, .support = {.food = -1}, .time = 1, .troops_count = 25, .requires = (1 << BuildingBarracks) | (1 << BuildingArcheryRange),
		.melee = {.weapon = WEAPON_CLEAVING, .damage = 1.0, .agility = 1.0},
		.ranged = {.weapon = WEAPON_ARROW, .damage = 2.0, .range = 6},
	},
	[UnitLightCavalry] = {
		.index = UnitLightCavalry, NAME("Light Cavalry"), .speed = 9, .health = 10, .armor = ARMOR_LEATHER,
		.cost = {.gold = -48, .wood = -15}, .support = {.food = -2}, .time = 2, .troops_count = 16, .requires = (1 << BuildingBarracks) | (1 << BuildingStables),
		.melee = {.weapon = WEAPON_CLEAVING, .damage = 2.0, .agility = 1.25},
	},
	[UnitBatteringRam] = {
		.index = UnitBatteringRam, NAME("Battering Ram"), .speed = 3, .health = 60, .armor = ARMOR_WOODEN,
		.cost = {.gold = -50, .wood = -100}, .support = {.food = -6}, .time = 2, .troops_count = 1, .requires = (1 << BuildingWorkshop),
		.melee = {.weapon = WEAPON_BLUNT, .damage = 50.0, .agility = 0.25},
	},
};

const struct building BUILDINGS[] =
{
	[BuildingFarm] = {NAME("Farm"), .cost = {.gold = -100}, .income = {.food = 5}, .time = 2},
	[BuildingIrrigation] = {NAME("Irrigation"), .cost = {.gold = -160}, .income = {.food = 5}, .time = 4, .requires = (1 << BuildingFarm)},
	[BuildingSawmill] = {NAME("Sawmill"), .cost = {.gold = -120}, .income = {.wood = 10}, .time = 3},
	[BuildingMine] = {NAME("Mine"), .cost = {.gold = -200, .wood = -80}, .income = {.stone = 10}, .time = 4},
	[BuildingBloomery] = {NAME("Bloomery"), .cost = {.gold = -350, .stone = -200}, .income = {.iron = 5}, .time = 5, .requires = (1 << BuildingMine)},
	[BuildingBarracks] = {NAME("Barracks"), .cost = {.gold = -180, .wood = -100, .stone = -50}, .time = 4},
	[BuildingArcheryRange] = {NAME("Archery range"), .cost = {.gold = -110, .wood = -80}, .time = 2},
	[BuildingStables] = {NAME("Stables"), .cost = {.gold = -220, .food = -200, .wood = -300}, .support = {.food = -50}, .time = 6, .requires = (1 << BuildingFarm)},
	[BuildingWatchTower] = {NAME("Watch tower"), .cost = {.gold = -80, .wood = -100}, .time = 2},
	[BuildingPalisade] = {NAME("Palisade"), .cost = {.gold = -300, .wood = -400}, .time = 4, .conflicts = (1 << BuildingFortress)},
	[BuildingFortress] = {NAME("Fortress"), .cost = {.gold = -800, .stone = -400}, .time = 8, .requires = (1 << BuildingPalisade)},
	[BuildingWorkshop] = {NAME("Workshop"), .cost = {.gold = -320, .wood = -200, .stone = -100}, .time = 5, .requires = (1 << BuildingSawmill)},
	[BuildingForge] = {NAME("Forge"), .cost = {.gold = -250, .wood = -200, .stone = -100, .iron = -40}, .time = 4, .requires = (1 << BuildingBarracks)},
};
const size_t BUILDINGS_COUNT = sizeof(BUILDINGS) / sizeof(*BUILDINGS);

#undef NAME

static int region_build(uint32_t *restrict built, const struct array_json *restrict list)
{
	size_t i, j;
	const union json *entry;

	for(i = 0; i < list->count; ++i)
	{
		entry = list->data[i];
		if (json_type(entry) != JSON_STRING) return -1;

		// TODO maybe use hash table here
		for(j = 0; j < BUILDINGS_COUNT; ++j)
			if ((entry->string.size == BUILDINGS[j].name_length) && !memcmp(entry->string.data, BUILDINGS[j].name, BUILDINGS[j].name_length))
			{
				*built |= (1 << j);
				goto next;
			}
		return -1;
next:
		;
	}

	return 0;
}

// TODO maybe use hash table here
static signed char building_find(const struct string *restrict name)
{
	size_t i;
	for(i = 0; i < BUILDINGS_COUNT; ++i)
		if ((name->size == BUILDINGS[i].name_length) && !memcmp(name->data, BUILDINGS[i].name, BUILDINGS[i].name_length))
			return i;
	LOG_ERROR("building %.*s not found", (int)name->size, name->data);
	return 0;
}

// TODO maybe use hash table
static const struct unit *unit_find(const struct string *restrict name)
{
	size_t i;
	for(i = 0; i < UNITS_COUNT; ++i)
		if ((name->size == UNITS[i].name_length) && !memcmp(name->data, UNITS[i].name, UNITS[i].name_length))
			return UNITS + i;
	LOG_ERROR("unit %.*s not found", (int)name->size, name->data);
	return 0;
}

static inline const char *type_name(enum json_type type)
{
	const char *names[] = {
		[JSON_NULL] = "null",
		[JSON_BOOLEAN] = "boolean",
		[JSON_INTEGER] = "integer",
		[JSON_REAL] = "real",
		[JSON_STRING] = "string",
		[JSON_ARRAY] = "array",
		[JSON_OBJECT] = "object",
	};
	return names[type];
}

static inline union json *value_get(const struct hashmap *restrict hashmap, const unsigned char *restrict key, size_t size, enum json_type type)
{
	union json **entry = hashmap_get(hashmap, key, size);
	if (!entry)
		return 0;
	if (json_type(*entry) != type)
	{
		LOG_WARNING("%.*s is of type %s (expected: %s)", (int)size, key, type_name(json_type(*entry)), type_name(type));
		return 0;
	}
	return *entry;
}
#define value_get(hashmap, key, type) (value_get)(hashmap, key, sizeof(key) - 1, type)

static int region_init(struct game *restrict game, struct region *restrict region, const unsigned char *restrict name_data, size_t name_size, const struct hashmap *restrict data)
{
	size_t j;

	union json *item, *field, *entry;
	const union json *name, *count, *owner;
	const union json *x, *y;
	struct point *points;
	const struct unit *restrict unit;

	if (name_size > NAME_LIMIT) return -1;
	memcpy(region->name, name_data, name_size);
	region->name_length = name_size;

	item = value_get(data, "owner", JSON_INTEGER);
	if (!item) return -1;
	region->owner = item->integer;

	item = value_get(data, "population", JSON_INTEGER);
	if (!item || (item->integer < 1)) return -1;
	region->population = item->integer;

	if (item = value_get(data, "workers", JSON_OBJECT))
	{
		field = value_get(&item->object, "food", JSON_INTEGER);
		if (!field || (field->integer < 0)) return -1;
		region->workers.food = field->integer;

		field = value_get(&item->object, "wood", JSON_INTEGER);
		if (!field || (field->integer < 0)) return -1;
		region->workers.wood = field->integer;

		field = value_get(&item->object, "iron", JSON_INTEGER);
		if (!field || (field->integer < 0)) return -1;
		region->workers.iron = field->integer;

		field = value_get(&item->object, "strone", JSON_INTEGER);
		if (!field || (field->integer < 0)) return -1;
		region->workers.stone = field->integer;

		if ((region->workers.food + region->workers.wood + region->workers.iron + region->workers.stone) > 100)
			return -1;
	}
	else
	{
		region->workers.food = 40;
		region->workers.wood = 0;
		region->workers.iron = 0;
		region->workers.stone = 0;
	}

	region->garrison.owner = region->owner;
	region->garrison.siege = 0;

	region->troops = 0;

	if (item = value_get(data, "garrison", JSON_OBJECT))
	{
		field = value_get(&item->object, "owner", JSON_INTEGER);
		if (field) region->garrison.owner = field->integer;

		if (field = value_get(&item->object, "troops", JSON_ARRAY))
		{
			for(j = 0; j < field->array.count; ++j)
			{
				entry = field->array.data[j];
				if ((json_type(entry) != JSON_ARRAY) || (entry->array.count != 3)) return -1;
				name = entry->array.data[0];
				if (json_type(name) != JSON_STRING) return -1;
				count = entry->array.data[1];
				if ((json_type(count) != JSON_INTEGER) || (count->integer <= 0)) return -1;
				owner = entry->array.data[2];
				if ((json_type(owner) != JSON_INTEGER) || (owner->integer < 0) || (owner->integer >= game->players_count)) return -1;

				unit = unit_find(&name->string);
				if (!unit) return -1;
				if (troop_spawn(LOCATION_GARRISON, &region->troops, unit, count->integer, owner->integer)) return -1;
			}
		}

		if (field = value_get(&item->object, "siege", JSON_INTEGER))
			region->garrison.siege = field->integer;
	}

	if (item = value_get(data, "troops", JSON_ARRAY))
	{
		for(j = 0; j < item->array.count; ++j)
		{
			entry = item->array.data[j];
			if ((json_type(entry) != JSON_ARRAY) || (entry->array.count != 3)) return -1;
			name = entry->array.data[0];
			if (json_type(name) != JSON_STRING) return -1;
			count = entry->array.data[1];
			if ((json_type(count) != JSON_INTEGER) || (count->integer <= 0)) return -1;
			owner = entry->array.data[2];
			if ((json_type(owner) != JSON_INTEGER) || (owner->integer < 0) || (owner->integer >= game->players_count)) return -1;

			unit = unit_find(&name->string);
			if (!unit) return -1;
			if (troop_spawn(region, &region->troops, unit, count->integer, owner->integer)) return -1;
		}
	}

	size_t train_index = 0;
	region->train_progress = 0;
	if (item = value_get(data, "train", JSON_ARRAY))
	{
		if (item->array.count > TRAIN_QUEUE) return -1;

		for(j = 0; j < item->array.count; ++j)
		{
			name = item->array.data[j];
			if (json_type(name) != JSON_STRING) return -1;

			region->train[train_index] = unit_find(&name->string);
			if (!region->train[train_index]) return -1;
			train_index += 1;
		}

		if (item = value_get(data, "train_progress", JSON_INTEGER))
			region->train_progress = item->integer;
	}
	while (train_index < TRAIN_QUEUE)
		region->train[train_index++] = 0;

	region->built = 0;
	if (item = value_get(data, "built", JSON_ARRAY))
		if (region_build(&region->built, &item->array))
			return -1;

	region->build_progress = 0;
	if (item = value_get(data, "construct", JSON_STRING))
	{
		signed char building;

		building = building_find(&item->string);
		if (building < 0) return -1;
		region->construct = building;

		if (item = value_get(data, "build_progress", JSON_INTEGER))
			region->build_progress = item->integer;
	}
	else region->construct = -1;

	item = value_get(data, "location", JSON_ARRAY);
	if (!item || (item->array.count < 3)) return -1;
	region->location = malloc(offsetof(struct polygon, points) + item->array.count * sizeof(struct point));
	if (!region->location) return -1;
	region->location->vertices_count = item->array.count;
	points = region->location->points;
	for(j = 0; j < item->array.count; ++j)
	{
		entry = item->array.data[j];
		if ((json_type(entry) != JSON_ARRAY) || (entry->array.count != 2))
		{
			free(region->location);
			return -1;
		}
		x = entry->array.data[0];
		y = entry->array.data[1];
		if ((json_type(x) != JSON_INTEGER) || (json_type(y) != JSON_INTEGER))
		{
			free(region->location);
			return -1;
		}
		points[j] = (struct point){x->integer, y->integer};
	}

	item = value_get(data, "location_garrison", JSON_ARRAY);
	if (!item || (item->array.count != 2)) return -1;
	x = item->array.data[0];
	y = item->array.data[1];
	if ((json_type(x) != JSON_INTEGER) || (json_type(y) != JSON_INTEGER)) return -1;
	region->location_garrison = (struct point){x->integer, y->integer};

	item = value_get(data, "center", JSON_ARRAY);
	if (!item || (item->array.count != 2)) return -1;
	x = item->array.data[0];
	y = item->array.data[1];
	if ((json_type(x) != JSON_INTEGER) || (json_type(y) != JSON_INTEGER)) return -1;
	region->center = (struct point){x->integer, y->integer};

	return 0;
}

static int region_neighbors(struct game *restrict game, struct region *restrict region, const struct hashmap *restrict regions, const struct hashmap *restrict data)
{
	size_t i;

	union json *node, *item, *entry;

	size_t index;

	item = value_get(data, "neighbors", JSON_ARRAY);
	if (!item || (item->array.count != NEIGHBORS_LIMIT)) return -1;
	for(i = 0; i < NEIGHBORS_LIMIT; ++i)
	{
		entry = item->array.data[i];
		switch (json_type(entry))
		{
		case JSON_NULL:
			region->neighbors[i] = 0;
			continue; // no neighbor in this direction
		default:
			return -1;
		case JSON_STRING:
			break;
		}

		node = (value_get)(regions, entry->string.data, entry->string.size, JSON_OBJECT);
		if (!node) return -1; // no region with such name
		index = value_get(&node->object, "location", JSON_ARRAY)->integer; // TODO ugly: type is not changed but the value is integer
		region->neighbors[i] = game->regions + index;
	}

	return 0;
}

static int world_populate(const union json *restrict json, struct game *restrict game)
{
	const union json *item, *node, *field;
	size_t index;
	size_t i;

	int local_initialized = 0;

	game->players_count = 0;
	game->players = 0;
	game->regions_count = 0;
	game->regions = 0;

	game->turn = 0; // TODO get this from the world file

	if (json_type(json) != JSON_OBJECT) goto error;
	node = value_get(&json->object, "players", JSON_ARRAY);
	if (!node || (node->array.count < 1) || (node->array.count > PLAYERS_LIMIT)) goto error;

	game->players_count = node->array.count;
	game->players = malloc(game->players_count * sizeof(struct player));
	if (!game->players) goto error;

	for(index = 0; index < game->players_count; ++index)
	{
		item = node->array.data[index];
		if (json_type(item) != JSON_OBJECT) goto error;

		field = value_get(&item->object, "name", JSON_STRING);
		if (!field || (field->string.size > NAME_LIMIT)) goto error;
		memcpy(game->players[index].name, field->string.data, field->string.size);
		game->players[index].name_length = field->string.size;

		field = value_get(&item->object, "alliance", JSON_INTEGER);
		if (!field || (field->integer >= PLAYERS_LIMIT)) goto error;
		game->players[index].alliance = field->integer;

		field = value_get(&item->object, "gold", JSON_INTEGER);
		if (!field) goto error;
		game->players[index].treasury.gold = field->integer;

		field = value_get(&item->object, "food", JSON_INTEGER);
		if (!field) goto error;
		game->players[index].treasury.food = field->integer;

		field = value_get(&item->object, "iron", JSON_INTEGER);
		if (!field) goto error;
		game->players[index].treasury.iron = field->integer;

		field = value_get(&item->object, "wood", JSON_INTEGER);
		if (!field) goto error;
		game->players[index].treasury.wood = field->integer;

		field = value_get(&item->object, "stone", JSON_INTEGER);
		if (!field) goto error;
		game->players[index].treasury.stone = field->integer;

		if (index == PLAYER_NEUTRAL) game->players[index].type = Neutral;
		else if (local_initialized) game->players[index].type = Computer;
		else
		{
			game->players[index].type = Local;
			local_initialized = 1;
		}
	}

	node = value_get(&json->object, "regions", JSON_OBJECT);
	if (!node || (node->object.count < 1) || (node->object.count > REGIONS_LIMIT)) goto error;

	game->regions_count = node->object.count;
	game->regions = malloc(game->regions_count * sizeof(struct region));
	if (!game->regions) goto error;
	for(i = 0; i < game->regions_count; ++i)
		game->regions[i].location = 0;

	struct hashmap_iterator it;
	struct hashmap_entry *region;
	index = 0;
	for(region = hashmap_first(&node->object, &it); region; region = hashmap_next(&node->object, &it))
	{
		item = region->value;
		if (json_type(item) != JSON_OBJECT) goto error;

		game->regions[index].index = index;
		if (region_init(game, game->regions + index, region->key_data, region->key_size, &item->object) < 0)
		{
			LOG_ERROR("Failed initializing %.*s", (int)region->key_size, region->key_data);
			goto error;
		}

		// TODO very ugly way to store a hashmap with region indices; fix this
		union json *field = value_get(&item->object, "location", JSON_ARRAY);
		for(i = 0; i < field->array.count; ++i)
			json_free(field->array.data[i]);
		field->integer = index;

		index += 1;
	}

	for(region = hashmap_first(&node->object, &it); region; region = hashmap_next(&node->object, &it))
	{
		item = region->value;
		index = value_get(&item->object, "location", JSON_ARRAY)->integer; // TODO ugly: type is not changed but the value is integer
		if (region_neighbors(game, game->regions + index, &node->object, &item->object) < 0)
			goto error;
	}

	// TODO Initialize turn number and month names.

	return 0;

error:
	// TODO segmentation fault on partial initialization (because of non-zeroed pointers)
	world_unload(game);
	return ERROR_INPUT;
}

int world_load(const unsigned char *restrict filepath, struct game *restrict game)
{
	int file;
	struct stat info;
	unsigned char *buffer;
	int status;
	union json *json;

	// Read file content.
	file = open(filepath, O_RDONLY);
	if (file < 0) return ERROR_MISSING; // TODO this could be ERROR_ACCESS or something else
	if (fstat(file, &info) < 0)
	{
		close(file);
		return ERROR_MISSING; // TODO this could be ERROR_ACCESS or something else
	}
	buffer = mmap(0, info.st_size, PROT_READ, MAP_SHARED, file, 0);
	close(file);
	if (buffer == MAP_FAILED) return ERROR_MEMORY;

	// Parse file content.
	json = json_parse(buffer, info.st_size);
	munmap(buffer, info.st_size);
	if (!json) return ERROR_INPUT;

	// Populate game data.
	status = world_populate(json, game);
	json_free(json);

	return status;
}

static union json *world_save_point(struct point p)
{
	union json *point = json_array();
	point = json_array_insert(point, json_integer(p.x));
	point = json_array_insert(point, json_integer(p.y));
	return point;
}

static union json *world_save_troops(const struct troop *troop, const struct region *restrict location)
{
	union json *troops = json_array();
	for(; troop; troop = troop->_next)
	{
		union json *t;

		if (troop->location != location) continue;

		t = json_array();
		t = json_array_insert(t, json_string(troop->unit->name, troop->unit->name_length));
		t = json_array_insert(t, json_integer(troop->count));
		t = json_array_insert(t, json_integer(troop->owner));
		troops = json_array_insert(troops, t);
	}
	return troops;
}

static union json *world_store(const struct game *restrict game)
{
	union json *json = json_object();

	size_t i, j;

	union json *players = json_array();
	for(i = 0; i < game->players_count; ++i)
	{
		union json *player = json_object();

		player = json_object_insert(player, S("name"), json_string(game->players[i].name, game->players[i].name_length));
		player = json_object_insert(player, S("alliance"), json_integer(game->players[i].alliance));
		player = json_object_insert(player, S("gold"), json_integer(game->players[i].treasury.gold));
		player = json_object_insert(player, S("food"), json_integer(game->players[i].treasury.food));
		player = json_object_insert(player, S("wood"), json_integer(game->players[i].treasury.wood));
		player = json_object_insert(player, S("iron"), json_integer(game->players[i].treasury.iron));
		player = json_object_insert(player, S("stone"), json_integer(game->players[i].treasury.stone));

		players = json_array_insert(players, player);
	}
	json = json_object_insert(json, S("players"), players);

	union json *regions = json_object();
	for(i = 0; i < game->regions_count; ++i)
	{
		union json *region = json_object();

		union json *neighbors = json_array();
		for(j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			const struct region *restrict neighbor = game->regions[i].neighbors[j];
			if (neighbor) neighbors = json_array_insert(neighbors, json_string(neighbor->name, neighbor->name_length));
			else neighbors = json_array_insert(neighbors, json_null());
		}
		region = json_object_insert(region, S("neighbors"), neighbors);

		union json *location = json_array();
		const struct polygon *restrict polygon = game->regions[i].location;
		for(j = 0; j < polygon->vertices_count; ++j)
			location = json_array_insert(location, world_save_point(polygon->points[j]));
		region = json_object_insert(region, S("location"), location);

		region = json_object_insert(region, S("location_garrison"), world_save_point(game->regions[i].location_garrison));
		region = json_object_insert(region, S("center"), world_save_point(game->regions[i].center));

		region = json_object_insert(region, S("owner"), json_integer(game->regions[i].owner));

		region = json_object_insert(region, S("population"), json_integer(game->regions[i].population));
		union json *workers = json_object();
		workers = json_object_insert(workers, S("food"), json_integer(game->regions[i].workers.food));
		workers = json_object_insert(workers, S("wood"), json_integer(game->regions[i].workers.wood));
		workers = json_object_insert(workers, S("iron"), json_integer(game->regions[i].workers.iron));
		workers = json_object_insert(workers, S("stone"), json_integer(game->regions[i].workers.stone));
		region = json_object_insert(region, S("workers"), workers);

		union json *train = json_array();
		for(j = 0; j < TRAIN_QUEUE; ++j)
		{
			const struct unit *restrict unit = game->regions[i].train[j];
			if (!unit) break;
			train = json_array_insert(train, json_string(unit->name, unit->name_length));
		}
		region = json_object_insert(region, S("train"), train);
		region = json_object_insert(region, S("train_progress"), json_integer(game->regions[i].train_progress));

		union json *built = json_array();
		for(j = 0; j < BUILDINGS_COUNT; ++j)
			if (game->regions[i].built & (1 << j))
				built = json_array_insert(built, json_string(BUILDINGS[j].name, BUILDINGS[j].name_length));
		region = json_object_insert(region, S("built"), built);
		if (game->regions[i].construct >= 0)
		{
			const struct building *restrict building = &BUILDINGS[game->regions[i].construct];
			region = json_object_insert(region, S("construct"), json_string(building->name, building->name_length));
			region = json_object_insert(region, S("build_progress"), json_integer(game->regions[i].build_progress));
		}

		region = json_object_insert(region, S("troops"), world_save_troops(game->regions[i].troops, game->regions + i));

		if (garrison_info(game->regions + i))
		{
			union json *garrison = json_object();
			garrison = json_object_insert(garrison, S("owner"), json_integer(game->regions[i].garrison.owner));
			garrison = json_object_insert(garrison, S("troops"), world_save_troops(game->regions[i].troops, LOCATION_GARRISON));
			garrison = json_object_insert(garrison, S("siege"), json_integer(game->regions[i].garrison.siege));
			region = json_object_insert(region, S("garrison"), garrison);
		}

		regions = json_object_insert(regions, game->regions[i].name, game->regions[i].name_length, region);
	}
	json = json_object_insert(json, S("regions"), regions);

	// game->turn // TODO write this to the world file

	return json;
}

int world_save(const struct game *restrict game, const unsigned char *restrict filepath)
{
	union json *json;
	size_t size;
	unsigned char *buffer;
	int file;
	size_t progress;
	ssize_t written;

	json = world_store(game);
	if (!json) return ERROR_MEMORY;

	size = json_size(json);
	buffer = malloc(size);
	if (!buffer)
	{
		json_free(json);
		return ERROR_MEMORY;
	}

	json_dump(buffer, json);
	json_free(json);

	file = creat(filepath, 0644);
	if (file < 0)
	{
		free(buffer);
		return ERROR_ACCESS; // TODO this could be several different errors
	}

	// Write the serialized world into the file.
	for(progress = 0; progress < size; progress += written)
	{
		written = write(file, buffer + progress, size - progress);
		if (written < 0)
		{
			unlink(filepath);
			close(file);
			free(buffer);
			return ERROR_WRITE;
		}
	}
	write(file, "\n", 1); // TODO this can fail

	close(file);
	free(buffer);
	return 0;
}

void world_unload(struct game *restrict game)
{
	if (game->regions)
		for(size_t i = 0; i < game->regions_count; ++i)
			free(game->regions[i].location);
	free(game->regions);
	free(game->players);
}

#undef value_get
