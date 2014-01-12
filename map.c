#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "battle.h"
#include "interface.h"

#define WINNER_NOBODY -1
#define WINNER_BATTLE -2

/* TODO
fix battle draw behavior
support more than 6 slots
*/

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

			regions[y][x].income = (struct resources){.gold = 1, .food = 2};

			memset((void *)regions[y][x].train, 0, sizeof(regions[y][x].train));

			regions[y][x].x = x;
			regions[y][x].y = y;
		}
	}

	regions[3][5].owner = 1;
	regions[6][5].owner = 1;
	regions[4][6].owner = 2;
	regions[2][3].owner = 3;

	if_regions(regions);

	struct slot *slot;
	size_t index;

	unsigned char alliance;
	signed char winner;

	struct pawn *pawns;
	unsigned char owner, slots;

	while (1)
	{
		// Ask each player to perform map actions.
		for(player = 1; player < players_count; ++player) // TODO skip player 0 in a natural way
		{
			// TODO skip dead players

			if (input_map(player, players) < 0) return;
		}

		for(y = 0; y < MAP_HEIGHT; ++y)
		{
			for(x = 0; x < MAP_WIDTH; ++x)
			{
				region = regions[y] + x;

				// The first training unit finished the training. Add it to the region.
				if (region->train[0] && resource_enough(&players[region->owner].treasury, &region->train[0]->cost))
				{
					// Spend the money required for the unit.
					resource_spend(&players[region->owner].treasury, &region->train[0]->cost);

					slot = malloc(sizeof(*slot));
					if (!slot) ; // TODO
					slot->unit = region->train[0];
					slot->count = 10; // TODO fix this
					slot->player = region->owner;

					slot->_prev = 0;
					slot->_next = region->slots;
					if (region->slots) region->slots->_prev = slot;
					region->slots = slot;
					slot->move = slot->location = region;

					for(index = 1; index < TRAIN_QUEUE; ++index)
						region->train[index - 1] = region->train[index];
					region->train[TRAIN_QUEUE - 1] = 0;
				}

				// Move each unit for which movement is specified.
				for(slot = region->slots; slot; slot = slot->_next)
				{
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
				}
			}
		}

		for(y = 0; y < MAP_HEIGHT; ++y)
		{
			for(x = 0; x < MAP_WIDTH; ++x)
			{
				region = regions[y] + x;

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

				if (winner == WINNER_BATTLE) winner = battle(players, players_count, region->slots); // TODO distinguish between error and draw

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
				if (region->owner) // TODO don't add income to netural players in a natural way
					resource_collect(&players[region->owner].treasury, &region->income);
			}
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
