#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "types.h"
#include "json.h"
#include "map.h"
#include "battle.h"
#include "interface.h"

#define S(s) s, sizeof(s) - 1

#define WINNER_NOBODY -1
#define WINNER_BATTLE -2

struct unit units[] = {
	{.index = 0, .health = 3, .damage = 1, .speed = 3, .cost = {.wood = -1}, .expense = {.food = -2}},
	{.index = 1, .health = 3, .damage = 1, .speed = 3, .cost = {.gold = -1, .wood = -2}, .expense = {.food = -2}, .shoot = 1, .range = 4},
};
size_t units_count = 2;

static int play(struct player *restrict players, size_t players_count, struct region *restrict regions, size_t regions_count)
{
	unsigned char player;
	struct region *region;

	size_t index;
	size_t i;

	struct slot *slot, *next;

	unsigned char alliance;
	uint16_t alliances; // this limits the alliance numbers to the number of bits
	signed char winner;

	unsigned char slots, owner_slot;

	struct resources expenses[PLAYERS_LIMIT];
	unsigned char alive[PLAYERS_LIMIT];

	if_regions(regions, regions_count, units, units_count);

	do
	{
		memset(expenses, 0, sizeof(expenses));
		memset(alive, 0, sizeof(alive));

		// Ask each player to perform map actions.
		// TODO implement Computer and Remote
		for(player = 0; player < players_count; ++player)
			switch (players[player].type)
			{
			case Neutral:
				continue;

			case Local:
				if (input_map(player, players) < 0) return WINNER_NOBODY; // TODO
				break;
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
			// If there are slots of just one alliance and the region's owner is not in it, change region's owner to the owner of a random slot.

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
					owner_slot = random() % slots;

					slots = 0;
					for(slot = region->slots; slot; slot = slot->_next)
					{
						if (slots == owner_slot)
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

			// Each player that controls a region is alive.
			alive[region->owner] = 1;
		}

		// Perform player-specific actions.
		for(player = 0; player < players_count; ++player)
		{
			if (players[player].type == Neutral) continue;

			// Set player as dead, if not marked as alive.
			if (alive[player])
			{
				// Mark the alliance of each alive player as alive.
				alliances |= (1 << players[player].alliance);
			}
			else players[player].type = Neutral;

			// Subtract the player's expenses from the treasury.
			resource_spend(&players[player].treasury, expenses + player);
		}
	} while (alliances & (alliances - 1)); // while there is more than 1 alliance

	return alliances;
}

int main(int argc, char *argv[])
{
	struct stat info;
	int file;
	char *buffer;
	struct string dump;
	union json *json;
	int success;

	struct game game;
	int winner;

	if (argc < 2)
	{
		write(2, S("You must specify map\n"));
		return 0;
	}

	file = open(argv[1], O_RDONLY);
	if (file < 0) return -1;
	if (fstat(file, &info) < 0)
	{
		close(file);
		return -1;
	}
	buffer = mmap(0, info.st_size, PROT_READ, MAP_SHARED, file, 0);
	close(file);
	if (buffer == MAP_FAILED) return -1;

	dump = string(buffer, info.st_size);
	json = json_parse(&dump);
	munmap(buffer, info.st_size);

	if (!json)
	{
		write(2, S("Invalid map format\n"));
		return -1;
	}
	success = !map_init(json, &game);
	json_free(json);
	if (!success)
	{
		write(2, S("Invalid map data\n"));
		return -1;
	}

	srandom(time(0));
	if_init();

	winner = play(game.players, game.players_count, game.regions, game.regions_count);

	// TODO display game end message
	write(2, S("game finished\n"));

	map_term(game.players, game.players_count, game.regions, game.regions_count);

	return 0;
}
