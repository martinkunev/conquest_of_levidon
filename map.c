#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "battle.h"
#include "interface.h"

struct unit peasant = {.health = 3, .damage = 1, .speed = 3, .cost = {.gold = 1, .food = 2}};

void map_init(struct player *restrict players, size_t players_count)
{
	struct region regions[MAP_HEIGHT][MAP_WIDTH];

	struct region *region;

	unsigned char x, y;

	unsigned char player;

	for(y = 0; y < MAP_HEIGHT; ++y)
	{
		for(x = 0; x < MAP_WIDTH; ++x)
		{
			regions[y][x].owner = 0;
			regions[y][x].slots = 0;

			regions[y][x].neighbors[0] = (((x + 1) < MAP_WIDTH) ? (regions[y] + x + 1) : 0);
			regions[y][x].neighbors[2] = (y ? (regions[y - 1] + x) : 0);
			regions[y][x].neighbors[4] = (x ? (regions[y] + x - 1) : 0);
			regions[y][x].neighbors[6] = (((y + 1) < MAP_HEIGHT) ? (regions[y + 1] + x) : 0);

			regions[y][x].neighbors[1] = ((regions[y][x].neighbors[0] && regions[y][x].neighbors[2]) ? (regions[y - 1] + x + 1) : 0);
			regions[y][x].neighbors[3] = ((regions[y][x].neighbors[2] && regions[y][x].neighbors[4]) ? (regions[y - 1] + x - 1) : 0);
			regions[y][x].neighbors[5] = ((regions[y][x].neighbors[4] && regions[y][x].neighbors[6]) ? (regions[y + 1] + x - 1) : 0);
			regions[y][x].neighbors[7] = ((regions[y][x].neighbors[6] && regions[y][x].neighbors[0]) ? (regions[y + 1] + x + 1) : 0);

			regions[y][x].income = (struct resources){.gold = 1, .food = 1};

			memset((void *)regions[y][x].train, 0, sizeof(regions[y][x].train));
		}
	}

	regions[3][5].owner = 1;
	regions[8][1].owner = 2;
	regions[9][5].owner = 1;
	regions[11][0].owner = 3;

	if_regions(regions);

	struct slot *slot;
	size_t index;

	while (1)
	{
		// Ask each player to perform map actions.
		for(player = 1; player < players_count; ++player) // TODO skip player 0 in a natural way
		{
			// TODO skip dead players

			if (input_map(player, players) < 0) return;
		}

		// Perform region-specific actions.
		for(y = 0; y < MAP_HEIGHT; ++y)
			for(x = 0; x < MAP_WIDTH; ++x)
			{
				region = regions[y] + x;

				// The first training unit finished the training. Add it to the region.
				if (region->train[0] && resource_enough(&players[region->owner].treasury, &region->train[0]->cost))
				{
					// Spend the money required by the unit.
					resource_spend(&players[region->owner].treasury, &region->train[0]->cost);

					slot = malloc(sizeof(*slot));
					if (!slot) ; // TODO
					slot->unit = region->train[0];
					slot->count = 10; // TODO fix this
					slot->player = region->owner;

					slot->_prev = 0;
					slot->_next = region->slots;
					region->slots = slot;

					for(index = 1; index < TRAIN_QUEUE; ++index)
						region->train[index - 1] = region->train[index];
					region->train[TRAIN_QUEUE - 1] = 0;
				}
			}
		for(y = 0; y < MAP_HEIGHT; ++y)
			for(x = 0; x < MAP_WIDTH; ++x)
			{
				// Add the income from each region to the owner's treasury.
				if (regions[y][x].owner)
					resource_collect(&players[regions[y][x].owner].treasury, &regions[y][x].income);
			}
	}
}

int main(void)
{
	srandom(time(0));

	if_init();

	struct player players[] = {{.alliance = 0}, {.alliance = 1}, {.alliance = 2}, {.alliance = 3}};

	map_init(players, sizeof(players) / sizeof(*players));
	return 0;
}
