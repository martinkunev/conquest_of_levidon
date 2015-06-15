#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "types.h"
#include "json.h"
#include "map.h"
#include "world.h"

// Player 0 and alliance 0 are hard-coded as neutral.

#define NAME(string) .name = string, .name_length = sizeof(string) - 1

const struct unit UNITS[] =
{
	{NAME("Peasant"), .index = 0, .health = 3, .damage = 1, .speed = 4, .cost = {.gold = 1}, .expense = {.food = 1}, .time = 1},
	{NAME("Archer"), .index = 1, .health = 3, .damage = 1, .speed = 4, .cost = {.gold = 1, .wood = 1}, .expense = {.food = 1}, .shoot = 1, .range = 5, .time = 1, .requires = (1 << BuildingArcheryRange)},
	{NAME("Militia"), .index = 2, .health = 5, .damage = 2, .speed = 5, .cost = {.gold = 1, .iron = 1}, .expense = {.food = 1}, .time = 1, .requires = (1 << BuildingBarracks)},
	{NAME("Light cavalry"), .index = 3, .health = 8, .damage = 2, .speed = 9, .cost = {.gold = 2, .iron = 1}, .expense = {.food = 3}, .time = 2, .requires = (1 << BuildingStables)},
};
const size_t UNITS_COUNT = sizeof(UNITS) / sizeof(*UNITS);

const struct building buildings[] =
{
	[BuildingFarm] = {NAME("Farm"), .cost = {.gold = 3}, .income = {.food = 2}, .time = 2},
	[BuildingIrrigation] = {NAME("Irrigation"), .cost = {.gold = 5}, .income = {.food = 2}, .time = 4, .requires = (1 << BuildingFarm)},
	[BuildingSawmill] = {NAME("Sawmill"), .cost = {.gold = 4}, .income = {.wood = 3}, .time = 3},
	[BuildingMine] = {NAME("Mine"), .cost = {.gold = 6, .wood = 6}, .income = {.stone = 3}, .time = 4},
	[BuildingBlastFurnace] = {NAME("Blast furnace"), .cost = {.gold = 8, .stone = 10}, .income = {.iron = 1, .stone = -1}, .time = 5, .requires = (1 << BuildingMine)},
	[BuildingBarracks] = {NAME("Barracks"), .cost = {.gold = 5, .stone = 10}, .time = 4},
	[BuildingArcheryRange] = {NAME("Archery range"), .cost = {.gold = 3, .wood = 4}, .time = 2},
	[BuildingStables] = {NAME("Stables"), .cost = {.gold = 10, .food = 10, .wood = 15}, .time = 4, .requires = (1 << BuildingFarm)},
	[BuildingWatchTower] = {NAME("Watch tower"), .cost = {.gold = 3, .wood = 5}, .time = 2},
	[BuildingPalisade] = {NAME("Palisade"), .cost = {.gold = 10, .wood = 20}, .time = 4},
	[BuildingFortress] = {NAME("Fortress"), .cost = {.gold = 20, .stone = 20}, .time = 8, .requires = (1 << BuildingPalisade)},
//	[BuildingMoat] = {NAME("Moat"), .cost = {.gold = 20, .wood = 10, .stone = 10}, .time = 7, .requires = (1 << BuildingPalisade)},
};
const size_t buildings_count = sizeof(buildings) / sizeof(*buildings);

#undef NAME

/*static struct polygon *region_create(size_t count, ...)
{
	size_t index;
	va_list vertices;

	// Allocate memory for the region and its vertices.
	struct polygon *polygon = malloc(sizeof(struct polygon) + count * sizeof(struct point));
	if (!polygon) return 0;
	polygon->vertices_count = count;

	// Initialize region vertices.
	va_start(vertices, count);
	for(index = 0; index < count; ++index)
		polygon->points[index] = va_arg(vertices, struct point);
	va_end(vertices);

	return polygon;
}*/

static int region_build(uint32_t *restrict built, const struct array *restrict list)
{
	size_t i, j;
	const union json *entry;

	for(i = 0; i < list->count; ++i)
	{
		entry = list->data[i];
		if (json_type(entry) != JSON_STRING) return -1;

		// TODO maybe use hash table here
		for(j = 0; j < buildings_count; ++j)
			if ((entry->string.size == buildings[j].name_length) && !memcmp(entry->string.data, buildings[j].name, buildings[j].name_length))
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

static int troop_create(struct troop **restrict troops, struct region *restrict region, const struct string *restrict name, unsigned count, unsigned char owner)
{
	size_t i;

	// TODO maybe use hash table
	for(i = 0; i < UNITS_COUNT; ++i)
		if ((name->size == UNITS[i].name_length) && !memcmp(name->data, UNITS[i].name, UNITS[i].name_length))
			return troop_spawn(region, troops, UNITS + i, count, owner);

	return -1;
}

#define GET(hashmap, key) *hashmap_get(hashmap, key, sizeof(key) - 1)

static int region_init(struct game *restrict game, struct region *restrict region, const struct hashmap *restrict data)
{
	size_t j;

	union json *item, *field, *entry;
	const union json *name, *count, *owner;
	const union json *x, *y;
	struct point *points;

	item = GET(data, "owner");
	if (!item || (json_type(item) != JSON_INTEGER)) return -1;
	region->owner = item->integer;

	region->garrison.position = North; // TODO fix this
	region->garrison.owner = region->owner;
	region->garrison.troops = 0;
	region->garrison.siege = 0;

	if (item = GET(data, "garrison"))
	{
		if (json_type(item) != JSON_OBJECT) return -1;

		/*field = GET(&item->object, "position");
		if (!field || (json_type(field) != JSON_INTEGER)) return -1;
		region->owner = field->integer;*/

		field = GET(&item->object, "owner");
		if (!field || (json_type(field) != JSON_INTEGER)) return -1;
		region->garrison.owner = field->integer;

		if (field = GET(&item->object, "troops"))
		{
			if (json_type(field) != JSON_ARRAY) return -1;
			for(j = 0; j < field->array.count; ++j)
			{
				entry = field->array.data[j];
				if ((json_type(entry) != JSON_ARRAY) || (entry->array.count != 3)) return -1;
				name = entry->array.data[0];
				if (json_type(name) != JSON_STRING) return -1;
				count = entry->array.data[1];
				if ((json_type(count) != JSON_INTEGER) || (count->integer <= 0)) return -1;
				owner = entry->array.data[2];
				if ((json_type(owner) != JSON_INTEGER) || (owner->integer <= 0) || (owner->integer >= game->players_count)) return -1;

				if (troop_create(&region->garrison.troops, region, &name->string, count->integer, owner->integer)) return -1;
			}
		}
	}

	region->troops = 0;
	if (item = GET(data, "troops"))
	{
		if (json_type(item) != JSON_ARRAY) return -1;
		for(j = 0; j < item->array.count; ++j)
		{
			entry = item->array.data[j];
			if ((json_type(entry) != JSON_ARRAY) || (entry->array.count != 3)) return -1;
			name = entry->array.data[0];
			if (json_type(name) != JSON_STRING) return -1;
			count = entry->array.data[1];
			if ((json_type(count) != JSON_INTEGER) || (count->integer <= 0)) return -1;
			owner = entry->array.data[2];
			if ((json_type(owner) != JSON_INTEGER) || (owner->integer <= 0) || (owner->integer >= game->players_count)) return -1;

			if (troop_create(&region->troops, region, &name->string, count->integer, owner->integer)) return -1;
		}
	}

	region->train_time = 0;
	memset(region->train, 0, sizeof(region->train)); // TODO implement this

	region->built = 0;
	region->construct = -1;
	region->build_progress = 0;
	if (item = GET(data, "built"))
	{
		if (json_type(item) != JSON_ARRAY) return -1;
		if (region_build(&region->built, &item->array)) return -1;
	}

	item = GET(data, "neighbors");
	if (!item || (json_type(item) != JSON_ARRAY) || (item->array.count != NEIGHBORS_LIMIT)) return -1;
	for(j = 0; j < NEIGHBORS_LIMIT; ++j)
	{
		entry = item->array.data[j];
		if (json_type(entry) != JSON_INTEGER) return -1;
		if ((entry->integer < 0) || (entry->integer >= game->regions_count)) region->neighbors[j] = 0;
		else region->neighbors[j] = game->regions + entry->integer;
	}

	item = GET(data, "center");
	if (!item || (json_type(item) != JSON_ARRAY) || (item->array.count != 2)) return -1;
	x = item->array.data[0];
	y = item->array.data[1];
	if ((json_type(x) != JSON_INTEGER) || (json_type(y) != JSON_INTEGER)) return -1;
	region->center = (struct point){x->integer, y->integer};

	item = GET(data, "name");
	if (!item || (json_type(item) != JSON_STRING) || (item->string.size > NAME_LIMIT)) return -1;
	memcpy(region->name, item->string.data, item->string.size);
	region->name_length = item->string.size;

	item = GET(data, "location");
	if (!item || (json_type(item) != JSON_ARRAY) || (item->array.count < 3)) return -1;
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

	return 0;
}

int world_init(const union json *restrict json, struct game *restrict game)
{
	struct string key;
	const union json *item, *node, *field, *entry;
	size_t index;

	game->players = 0;
	game->regions = 0;

	if (json_type(json) != JSON_OBJECT) goto error;

	node = GET(&json->object, "players");
	if (!node || (json_type(node) != JSON_ARRAY) || (node->array.count < 1) || (node->array.count > PLAYERS_LIMIT)) goto error;

	game->players_count = node->array.count;
	game->players = malloc(game->players_count * sizeof(struct player));
	if (!game->players) goto error;
	for(index = 0; index < game->players_count; ++index)
	{
		item = node->array.data[index];
		if (json_type(item) != JSON_OBJECT) goto error;

		field = GET(&item->object, "alliance");
		if (!field || (json_type(field) != JSON_INTEGER) || (field->integer >= PLAYERS_LIMIT)) goto error;
		game->players[index].alliance = field->integer;

		field = GET(&item->object, "gold");
		if (!field || (json_type(field) != JSON_INTEGER)) goto error;
		game->players[index].treasury.gold = field->integer;

		field = GET(&item->object, "food");
		if (!field || (json_type(field) != JSON_INTEGER)) goto error;
		game->players[index].treasury.food = field->integer;

		field = GET(&item->object, "iron");
		if (!field || (json_type(field) != JSON_INTEGER)) goto error;
		game->players[index].treasury.iron = field->integer;

		field = GET(&item->object, "wood");
		if (!field || (json_type(field) != JSON_INTEGER)) goto error;
		game->players[index].treasury.wood = field->integer;

		field = GET(&item->object, "stone");
		if (!field || (json_type(field) != JSON_INTEGER)) goto error;
		game->players[index].treasury.stone = field->integer;

		game->players[index].type = Local; // TODO set this
		//game->players[index].input_formation = input_formation;
	}

	// Player 0 is hard-coded as neutral.
	game->players[0].type = Neutral;
	//game->players[0].input_formation = input_formation_none;

	node = GET(&json->object, "regions");
	if (!node || (json_type(node) != JSON_ARRAY) || (node->array.count < 1) || (node->array.count > REGIONS_LIMIT)) goto error;

	game->regions_count = node->array.count;
	game->regions = malloc(game->regions_count * sizeof(struct region));
	if (!game->regions) goto error;
	for(index = 0; index < game->regions_count; ++index)
	{
		item = node->array.data[index];
		if (json_type(item) != JSON_OBJECT) goto error;

		game->regions[index].index = index;

		if (region_init(game, game->regions + index, &item->object) < 0) goto error;
		continue;

		////////////////////////
	}

	return 0;

error:
	// TODO segmentation fault on partial initialization? (because of non-zeroed pointers)
	world_term(game);
	return -1;
}

void world_term(struct game *restrict game)
{
	size_t index;
	for(index = 0; index < game->regions_count; ++index)
		free(game->regions[index].location);
	free(game->regions);
	free(game->players);
}

#undef GET
