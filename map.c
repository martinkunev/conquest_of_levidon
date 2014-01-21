#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "battle.h"
#include "interface.h"

#define WINNER_NOBODY -1
#define WINNER_BATTLE -2

/* TODO
shooting
support more than 6 slots
pawn start positions in battles
*/

struct unit units[] = {
	{.index = 0, .health = 3, .damage = 1, .speed = 3, .cost = {.food = 2}},
	{.index = 1, .health = 3, .damage = 1, .speed = 3, .cost = {.gold = 1, .food = 2}, .shoot = 1, .range = 4},
};
size_t units_count = 2;

#define REGIONS_MAX 256

static struct polygon *region_create(size_t count, ...)
{
	size_t index;
	va_list vertices;

	// Allocate memory for the region and its vertices.
	struct polygon *polygon = malloc(sizeof(struct polygon) + count * sizeof(struct point));
	polygon->count = count;

	// Initialize region vertices.
	va_start(vertices, count);
	for(index = 0; index < count; ++index)
		polygon->points[index] = va_arg(vertices, struct point);
	va_end(vertices);

	return polygon;
}

void map_init(struct player *restrict players, size_t players_count)
{
	struct region regions[REGIONS_MAX];
	size_t regions_count = 6;

	struct region *region;

	unsigned char player;

	size_t index;
	for(index = 0; index < regions_count; ++index)
	{
		regions[index].owner = 0;
		regions[index].slots = 0;

		regions[index].index = index;
		memset(regions[index].neighbors, 0, sizeof(regions[index].neighbors));

		regions[index].income = (struct resources){.gold = 1, .food = 2};
	}

	regions[0].location = region_create(4,
		(struct point){768, 768},
		(struct point){768, 500},
		(struct point){550, 550},
		(struct point){600, 768}
	);
	regions[0].neighbors[2] = regions + 1;
	regions[0].neighbors[4] = regions + 5;
	regions[0].center = (struct point){600, 650};

	regions[1].location = region_create(6,
		(struct point){600, 250},
		(struct point){550, 550},
		(struct point){768, 500},
		(struct point){768, 0},
		(struct point){384, 0},
		(struct point){384, 150}
	);
	regions[1].neighbors[4] = regions + 3;
	regions[1].neighbors[5] = regions + 2;
	regions[1].neighbors[6] = regions + 0;
	regions[1].center = (struct point){650, 200};

	regions[2].location = region_create(5,
		(struct point){550, 550},
		(struct point){600, 250},
		(struct point){384, 150},
		(struct point){150, 250},
		(struct point){200, 550}
	);
	regions[2].neighbors[1] = regions + 1;
	regions[2].neighbors[3] = regions + 3;
	regions[2].neighbors[4] = regions + 4;
	regions[2].neighbors[6] = regions + 5;
	regions[2].center = (struct point){384, 384};

	regions[3].location = region_create(5,
		(struct point){384, 150},
		(struct point){384, 0},
		(struct point){0, 0},
		(struct point){0, 200},
		(struct point){150, 250}
	);
	regions[3].neighbors[0] = regions + 1;
	regions[3].neighbors[7] = regions + 2;
	regions[3].neighbors[6] = regions + 4;
	regions[3].center = (struct point){200, 120};

	regions[4].location = region_create(4,
		(struct point){200, 550},
		(struct point){150, 250},
		(struct point){0, 200},
		(struct point){0, 768}
	);
	regions[4].neighbors[0] = regions + 2;
	regions[4].neighbors[2] = regions + 3;
	regions[4].neighbors[7] = regions + 5;
	regions[4].center = (struct point){100, 450};

	regions[5].location = region_create(4,
		(struct point){600, 768},
		(struct point){550, 550},
		(struct point){200, 550},
		(struct point){0, 768}
	);
	regions[5].neighbors[0] = regions + 0;
	regions[5].neighbors[2] = regions + 2;
	regions[5].neighbors[3] = regions + 4;
	regions[5].center = (struct point){450, 650};

	regions[0].owner = 1;
	regions[1].owner = 1;
	regions[3].owner = 2;
	regions[5].owner = 3;

	if_regions(regions, regions_count, units, units_count);

	struct slot *slot, *next;

	unsigned char alliance;
	signed char winner;

	unsigned char owner, slots;

	size_t i;

	while (1)
	{
		// Ask each player to perform map actions.
		for(player = 1; player < players_count; ++player) // TODO skip player 0 in a natural way
		{
			// TODO skip dead players

			if (input_map(player, players) < 0) return;
		}

		for(index = 0; index < regions_count; ++index)
		{
			region = regions + index;

			// The first training unit finished the training. Add it to the region.
			if (region->train[0] && resource_enough(&players[region->owner].treasury, &region->train[0]->cost))
			{
				// Spend the money required for the unit.
				resource_spend(&players[region->owner].treasury, &region->train[0]->cost);

				slot = malloc(sizeof(*slot));
				if (!slot) ; // TODO
				slot->unit = region->train[0];
				slot->count = 1; // TODO fix this
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
			resource_collect(&players[region->owner].treasury, &region->income);
		}
	}
}

int main(void)
{
	srandom(time(0));

	if_init();

	struct player players[] = {
		{.alliance = 0, .treasury = {.gold = 1, .food = 2}},
		{.alliance = 1, .treasury = {.gold = 1, .food = 2}},
		{.alliance = 2, .treasury = {.gold = 1, .food = 2}},
		{.alliance = 3, .treasury = {.gold = 1, .food = 2}}
	};

	map_init(players, sizeof(players) / sizeof(*players));
	return 0;
}
