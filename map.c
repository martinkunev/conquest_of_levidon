#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "types.h"
#include "json.h"
#include "map.h"

// Player 0 and alliance 0 are hard-coded as neutral.

#define NAME(s) .name = (s), .name_length = sizeof(s) - 1

const struct unit units[] =
{
	{NAME("Peasant"), .index = 0, .health = 3, .damage = 1, .speed = 3, .cost = {.gold = 1}, .expense = {.food = 1}, .time = 1},
	{NAME("Archer"), .index = 1, .health = 3, .damage = 1, .speed = 3, .cost = {.gold = 1, .wood = 1}, .expense = {.food = 1}, .shoot = 1, .range = 5, .time = 1, .requires = (1 << 5)},
	{NAME("Pikeman"), .index = 2, .health = 5, .damage = 2, .speed = 4, .cost = {.gold = 2, .iron = 1}, .expense = {.food = 1}, .time = 1, .requires = (1 << 4)},
	{NAME("Horse rider"), .index = 3, .health = 8, .damage = 2, .speed = 8, .cost = {.gold = 3, .iron = 1}, .expense = {.food = 3}, .time = 2, .requires = (1 << 6)},
};
const size_t units_count = sizeof(units) / sizeof(*units);

const struct building buildings[] =
{
	{NAME("Irrigation"), .cost = {.gold = 4}, .income = {.food = 2}, .time = 2},
	{NAME("Lumber mill"), .cost = {.gold = 6}, .income = {.wood = 2}, .time = 3},
	{NAME("Mine"), .cost = {.gold = 10, .wood = 4}, .income = {.stone = 2}, .time = 4},
	{NAME("Blast furnace"), .cost = {.gold = 10, .stone = 12}, .income = {.iron = 1, .stone = -1}, .time = 5, .requires = (1 << 2)},
	{NAME("Barracks"), .cost = {.gold = 10, .stone = 8}, .time = 4},
	{NAME("Archery range"), .cost = {.gold = 5, .wood = 5}, .time = 2},
	{NAME("Stables"), .cost = {.gold = 10, .food = 10, .wood = 10}, .time = 3, .requires = (1 << 4)},
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

static int map_build(uint32_t *restrict built, const struct vector *restrict list)
{
	size_t i, j;
	const union json *entry;

	for(i = 0; i < list->length; ++i)
	{
		entry = vector_get(list, i);
		if (json_type(entry) != STRING) return -1;

		// TODO maybe use hash table here
		for(j = 0; j < buildings_count; ++j)
			if ((entry->string_node.length == buildings[j].name_length) && !memcmp(entry->string_node.data, buildings[j].name, buildings[j].name_length))
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

int slot_spawn(struct region *restrict region, const struct unit *restrict unit, unsigned count, unsigned char owner)
{
	struct slot *slot = malloc(sizeof(*slot));
	if (!slot) return -1;

	slot->unit = unit;
	slot->count = count;
	slot->player = owner;

	slot->_prev = 0;
	slot->_next = region->slots;
	if (region->slots) region->slots->_prev = slot;
	region->slots = slot;
	slot->move = slot->location = region;

	return 0;
}

static int map_slot(struct region *restrict region, const struct string *restrict name, unsigned count, unsigned char owner)
{
	size_t i;

	// TODO maybe use hash table
	for(i = 0; i < units_count; ++i)
		if ((name->length == units[i].name_length) && !memcmp(name->data, units[i].name, units[i].name_length))
			return slot_spawn(region, units + i, count, owner);

	return -1;
}

int map_init(const union json *restrict json, struct game *restrict game)
{
	struct string key;
	const union json *item, *node, *field, *entry;
	const union json *x, *y;
	const union json *name, *count, *owner;
	size_t index;
	size_t i, j;
	struct point *points;

	game->players = 0;
	game->regions = 0;

	if (json_type(json) != OBJECT) goto error;

	key = string("players");
	node = dict_get(json->object, &key);
	if (!node || (json_type(node) != ARRAY) || (node->array_node.length < 1) || (node->array_node.length > PLAYERS_LIMIT)) goto error;

	game->players_count = node->array_node.length;
	game->players = malloc(game->players_count * sizeof(struct player));
	if (!game->players) goto error;
	for(index = 0; index < game->players_count; ++index)
	{
		item = node->array_node.data[index];
		if (json_type(item) != OBJECT) goto error;

		key = string("alliance");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER) || (field->integer >= PLAYERS_LIMIT)) goto error;
		game->players[index].alliance = field->integer;

		key = string("gold");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto error;
		game->players[index].treasury.gold = field->integer;

		key = string("food");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto error;
		game->players[index].treasury.food = field->integer;

		key = string("iron");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto error;
		game->players[index].treasury.iron = field->integer;

		key = string("wood");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto error;
		game->players[index].treasury.wood = field->integer;

		key = string("stone");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto error;
		game->players[index].treasury.stone = field->integer;

		game->players[index].type = Local; // TODO set this
	}

	// Player 0 is hard-coded as neutral.
	game->players[0].type = Neutral;

	key = string("regions");
	node = dict_get(json->object, &key);
	if (!node || (json_type(node) != ARRAY) || (node->array_node.length < 1) || (node->array_node.length > REGIONS_LIMIT)) goto error;

	game->regions_count = node->array_node.length;
	game->regions = malloc(game->regions_count * sizeof(struct region));
	if (!game->regions) goto error;
	for(index = 0; index < game->regions_count; ++index)
	{
		item = node->array_node.data[index];
		if (json_type(item) != OBJECT) goto error;

		game->regions[index].index = index;

		key = string("owner");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto error;
		game->regions[index].owner = field->integer;

		game->regions[index].slots = 0;
		key = string("army");
		if (field = dict_get(item->object, &key))
		{
			if (json_type(field) != ARRAY) goto error;
			for(j = 0; j < field->array_node.length; ++j)
			{
				entry = vector_get(&field->array_node, j);
				if ((json_type(entry) != ARRAY) || (entry->array_node.length != 3)) goto error;
				name = vector_get(&entry->array_node, 0);
				if (json_type(name) != STRING) goto error;
				count = vector_get(&entry->array_node, 1);
				if ((json_type(count) != INTEGER) || (count->integer <= 0)) goto error;
				owner = vector_get(&entry->array_node, 2);
				if ((json_type(owner) != INTEGER) || (owner->integer <= 0) || (owner->integer >= game->players_count)) goto error;

				if (map_slot(game->regions + index, &name->string_node, count->integer, owner->integer)) goto error;
			}
		}

		game->regions[index].train_time = 0;
		memset(game->regions[index].train, 0, sizeof(game->regions[index].train)); // TODO implement this

		game->regions[index].built = 0;
		game->regions[index].construct = -1;
		game->regions[index].construct_time = 0;
		key = string("built");
		if (field = dict_get(item->object, &key))
		{
			if (json_type(field) != ARRAY) goto error;
			if (map_build(&game->regions[index].built, &field->array_node)) goto error;
		}

		key = string("neighbors");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != ARRAY) || (field->array_node.length != NEIGHBORS_LIMIT)) goto error;
		for(j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			entry = vector_get(&field->array_node, j);
			if (json_type(entry) != INTEGER) goto error;
			if ((entry->integer < 0) || (entry->integer >= game->regions_count)) game->regions[index].neighbors[j] = 0;
			else game->regions[index].neighbors[j] = game->regions + entry->integer;
		}

		key = string("location");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != ARRAY) || (field->array_node.length < 3)) goto error;
		game->regions[index].location = malloc(sizeof(struct polygon) + field->array_node.length * sizeof(struct point));
		if (!game->regions[index].location) goto error;
		game->regions[index].location->vertices_count = field->array_node.length;
		points = game->regions[index].location->points;
		for(j = 0; j < field->array_node.length; ++j)
		{
			entry = vector_get(&field->array_node, j);
			if ((json_type(entry) != ARRAY) || (entry->array_node.length != 2)) goto error;
			x = vector_get(&entry->array_node, 0);
			y = vector_get(&entry->array_node, 1);
			points[j] = (struct point){x->integer, y->integer}; // TODO check vector element types
		}

		key = string("center");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != ARRAY) || (field->array_node.length != 2)) goto error;
		x = vector_get(&field->array_node, 0);
		y = vector_get(&field->array_node, 1);
		game->regions[index].center = (struct point){x->integer, y->integer}; // TODO check vector element types

		key = string("name");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != STRING) || (field->string_node.length > REGION_NAME_LIMIT)) goto error;
		memcpy(game->regions[index].name, field->string_node.data, field->string_node.length);
		game->regions[index].name_length = field->string_node.length;
	}

	game->units = units;
	game->units_count = units_count;

	return 0;

error:
	// TODO segmentation fault on partial initialization? (because of non-zeroed pointers)
	map_term(game);
	return -1;
}

void map_term(struct game *restrict game)
{
	size_t index;
	for(index = 0; index < game->regions_count; ++index)
		free(game->regions[index].location);
	free(game->regions);
	free(game->players);
}
