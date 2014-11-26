#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "types.h"
#include "json.h"
#include "battle.h"
#include "interface.h"

#define WINNER_NOBODY -1
#define WINNER_BATTLE -2

/* TODO
support more than 6 slots
*/

struct unit units[] = {
	{.index = 0, .health = 3, .damage = 1, .speed = 3, .cost = {.wood = -1}, .expense = {.food = -2}},
	{.index = 1, .health = 3, .damage = 1, .speed = 3, .cost = {.gold = -1, .wood = -2}, .expense = {.food = -2}, .shoot = 1, .range = 4},
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

void map_init(struct player *restrict players, size_t players_count, struct region *restrict regions, size_t regions_count)
{
	unsigned char player;
	struct region *region;

	size_t index;

	if_regions(regions, regions_count, units, units_count);

	struct resources *expenses = malloc(players_count * sizeof(*expenses));
	if (!expenses) return; // TODO

	struct slot *slot, *next;

	unsigned char alliance;
	signed char winner;

	unsigned char owner, slots;

	size_t i;

	while (1)
	{
		memset(expenses, 0, players_count * sizeof(*expenses));

		// Ask each player to perform map actions.
		for(player = 1; player < players_count; ++player) // TODO skip player 0 in a natural way
		{
			// TODO skip dead players

			if (input_map(player, players) < 0) return;
		}

		// Perform region-specific actions.

		for(index = 0; index < regions_count; ++index)
		{
			region = regions + index;

			for(slot = region->slots; slot; slot = slot->_next)
				resource_change(expenses + slot->player, &slot->unit->expense);

			// The first training unit finished the training. Add it to the region's slots.
			if (region->train[0] && resource_enough(&players[region->owner].treasury, &region->train[0]->cost))
			{
				// Spend the money required for the unit.
				resource_spend(&players[region->owner].treasury, &region->train[0]->cost);

				slot = malloc(sizeof(*slot));
				if (!slot) ; // TODO
				slot->unit = region->train[0];
				slot->count = 16; // TODO fix this
				slot->player = region->owner;

				slot->_prev = 0;
				slot->_next = region->slots;
				if (region->slots) region->slots->_prev = slot;
				region->slots = slot;
				slot->move = slot->location = region;

				for(i = 1; i < TRAIN_QUEUE; ++i)
					region->train[i - 1] = region->train[i];
				region->train[TRAIN_QUEUE - 1] = 0;
			}

			// Move each unit for which movement is specified.
			slot = region->slots;
			while (slot)
			{
				next = slot->_next;
				if (slot->move != slot->location)
				{
					// Remove the slot from its current location.
					if (slot->_prev) slot->_prev->_next = slot->_next;
					else region->slots = slot->_next;
					if (slot->_next) slot->_next->_prev = slot->_prev;

					// Put the slot to its new location.
					slot->_prev = 0;
					slot->_next = slot->move->slots;
					if (slot->move->slots) slot->move->slots->_prev = slot;
					slot->move->slots = slot;
				}
				slot = next;
			}
		}

		for(index = 0; index < regions_count; ++index)
		{
			region = regions + index;

			winner = WINNER_NOBODY;

			// Start a battle if there are enemy units in the region.
			slots = 0;
			for(slot = region->slots; slot; slot = slot->_next)
			{
				alliance = players[slot->player].alliance;

				if (winner == WINNER_NOBODY) winner = alliance;
				else if (winner != alliance) winner = WINNER_BATTLE;

				slots += 1;
			}

			if (winner == WINNER_BATTLE) winner = battle(players, players_count, region);

			// winner - the number of the region's new owner

			// Only slots of a single alliance are allowed to stay in the region.
			// If there are slots of more than one alliance, return any slot owned by enemy of the region's owner to its initial location.
			// If there are slots of just one alliance and this alliance is enemy to the region's owner, change region's owner to the owner of a random slot.

			// TODO is it a good idea to choose owner based on number of slots?

			slots = 0;

			// Set the location of each unit.
			for(slot = region->slots; slot; slot = slot->_next)
			{
				// Remove dead slots.
				if (!slot->count)
				{
					if (slot->_prev) slot->_prev->_next = slot->_next;
					else region->slots = slot->_next;
					if (slot->_next) slot->_next->_prev = slot->_prev;
					free(slot);
					continue;
				}

				if (players[slot->player].alliance == winner)
				{
					slot->location = region;
					slots += 1;
				}
				else
				{
					if (slot->_prev) slot->_prev->_next = slot->_next;
					else region->slots = slot->_next;
					if (slot->_next) slot->_next->_prev = slot->_prev;

					// Put the slot back to its original location.
					slot->_prev = 0;
					slot->_next = slot->location->slots;
					if (slot->location->slots) slot->location->slots->_prev = slot;
					slot->location->slots = slot;
					slot->move = slot->location;
				}
			}

			if (winner != WINNER_NOBODY)
			{
				if (players[region->owner].alliance != winner)
				{
					// assert(slots);
					owner = random() % slots;

					slots = 0;
					for(slot = region->slots; slot; slot = slot->_next)
					{
						if (slots == owner)
						{
							region->owner = slot->player;
							break;
						}
						slots += 1;
					}
				}
			}

			// Add the income from each region to the owner's treasury.
			resource_change(&players[region->owner].treasury, &region->income);
		}

		// Subtract each player's expenses from the treasury.
		for(index = 0; index < players_count; ++index)
			resource_spend(&players[index].treasury, expenses + index);
	}

	free(expenses);
}

#include <stdio.h>
int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		fprintf(stderr, "You must specify map\n");
		return 0;
	}

	srandom(time(0));

	struct stat info;
	int fight = open(argv[1], O_RDONLY);
	if (fight < 0) return -1;
	if (fstat(fight, &info) < 0) return -1;
	char *buffer = mmap(0, info.st_size, PROT_READ, MAP_SHARED, fight, 0);
	close(fight);
	if (buffer == MAP_FAILED) return -1;

	struct string dump = string(buffer, info.st_size);
	union json *json = json_parse(&dump);
	munmap(buffer, info.st_size);

	if (!json)
	{
		printf("Invalid map\n");
		return -1;
	}

	struct string key;
	union json *item, *node, *field, *entry;
	union json *x, *y;
	size_t index;
	size_t j;
	struct point *points;

	struct player *players = 0;
	size_t players_count;

	struct region *regions = 0;
	size_t regions_count;

	if (json_type(json) != OBJECT) goto finally;

	key = string("players");
	node = dict_get(json->object, &key);
	if (!node || (json_type(node) != ARRAY) || (node->array_node.length < 1) || (node->array_node.length > PLAYERS_LIMIT)) goto finally;

	players_count = node->array_node.length;
	players = malloc(players_count * sizeof(struct player));
	if (!players) goto finally;
	for(index = 0; index < players_count; ++index)
	{
		item = node->array_node.data[index];
		if (json_type(item) != OBJECT) goto finally;

		key = string("alliance");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		players[index].alliance = field->integer;

		key = string("gold");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		players[index].treasury.gold = field->integer;

		key = string("food");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		players[index].treasury.food = field->integer;

		key = string("iron");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		players[index].treasury.iron = field->integer;

		key = string("wood");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		players[index].treasury.wood = field->integer;

		key = string("rock");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		players[index].treasury.rock = field->integer;

		players[index].type = Local; // TODO set this
	}

	key = string("regions");
	node = dict_get(json->object, &key);
	if (!node || (json_type(node) != ARRAY) || (node->array_node.length < 1) || (node->array_node.length > REGIONS_LIMIT)) goto finally;

	regions_count = node->array_node.length;
	regions = malloc(regions_count * sizeof(struct region));
	if (!regions) goto finally;
	for(index = 0; index < regions_count; ++index)
	{
		item = node->array_node.data[index];
		if (json_type(item) != OBJECT) goto finally;

		regions[index].index = index;

		key = string("owner");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		regions[index].owner = field->integer;

		regions[index].slots = 0; // TODO implement this

		memset(regions[index].train, 0, sizeof(regions[index].train)); // TODO implement this

		key = string("gold");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		regions[index].income.gold = field->integer;

		key = string("food");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		regions[index].income.food = field->integer;

		key = string("iron");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		regions[index].income.iron = field->integer;

		key = string("wood");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		regions[index].income.wood = field->integer;

		key = string("rock");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != INTEGER)) goto finally;
		regions[index].income.rock = field->integer;

		key = string("neighbors");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != ARRAY) || (field->array_node.length != NEIGHBORS_LIMIT)) goto finally;
		for(j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			entry = vector_get(&field->array_node, j);
			if (json_type(entry) != INTEGER) goto finally;
			if ((entry->integer < 0) || (entry->integer >= regions_count)) regions[index].neighbors[j] = 0;
			else regions[index].neighbors[j] = regions + entry->integer;
		}

		key = string("location");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != ARRAY) || (field->array_node.length < 3)) goto finally;
		regions[index].location = malloc(sizeof(struct polygon) + field->array_node.length * sizeof(struct point));
		if (!regions[index].location) goto finally;
		regions[index].location->vertices = field->array_node.length;
		points = regions[index].location->points;
		for(j = 0; j < field->array_node.length; ++j)
		{
			entry = vector_get(&field->array_node, j);
			if ((json_type(entry) != ARRAY) || (entry->array_node.length != 2)) goto finally;
			x = vector_get(&entry->array_node, 0);
			y = vector_get(&entry->array_node, 1);
			points[j] = (struct point){x->integer, y->integer}; // TODO check vector element types
		}

		key = string("center");
		field = dict_get(item->object, &key);
		if (!field || (json_type(field) != ARRAY) || (field->array_node.length != 2)) goto finally;
		x = vector_get(&field->array_node, 0);
		y = vector_get(&field->array_node, 1);
		regions[index].center = (struct point){x->integer, y->integer}; // TODO check vector element types
	}

	if_init();

	map_init(players, players_count, regions, regions_count);

finally:
	// TODO free region locations
	free(regions);
	free(players);
	json_free(json);
	return 0;
}
