#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "types.h"
#include "json.h"
#include "map.h"

// Player 0 and alliance 0 are hard-coded as neutral.

// TODO support more than 7 slots

struct unit units[] = {
	{.index = 0, .health = 3, .damage = 1, .speed = 3, .cost = {.wood = -1}, .expense = {.food = -2}},
	{.index = 1, .health = 3, .damage = 1, .speed = 3, .cost = {.gold = -1, .wood = -2}, .expense = {.food = -2}, .shoot = 1, .range = 4},
	{.index = 2, .health = 8, .damage = 2, .speed = 8, .cost = {.gold = -2, .wood = -2}, .expense = {.food = -5}},
};
size_t units_count = 2;

static struct polygon *region_create(size_t count, ...)
{
	size_t index;
	va_list vertices;

	// Allocate memory for the region and its vertices.
	struct polygon *polygon = malloc(sizeof(struct polygon) + count * sizeof(struct point));
	if (!polygon) return 0;
	polygon->vertices = count;

	// Initialize region vertices.
	va_start(vertices, count);
	for(index = 0; index < count; ++index)
		polygon->points[index] = va_arg(vertices, struct point);
	va_end(vertices);

	return polygon;
}

int map_init(const union json *restrict json, struct game *restrict game)
{
	struct string key;
	const union json *item, *node, *field, *entry;
	const union json *x, *y;
	size_t index;
	size_t j;
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

		key = string("rock");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto error;
		game->players[index].treasury.rock = field->integer;

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

		game->regions[index].slots = 0; // TODO implement this

		memset(game->regions[index].train, 0, sizeof(game->regions[index].train)); // TODO implement this

		key = string("gold");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto error;
		game->regions[index].income.gold = field->integer;

		key = string("food");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto error;
		game->regions[index].income.food = field->integer;

		key = string("iron");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto error;
		game->regions[index].income.iron = field->integer;

		key = string("wood");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto error;
		game->regions[index].income.wood = field->integer;

		key = string("rock");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto error;
		game->regions[index].income.rock = field->integer;

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
		game->regions[index].location->vertices = field->array_node.length;
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
