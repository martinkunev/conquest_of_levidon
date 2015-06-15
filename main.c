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
#include "world.h"
#include "pathfinding.h"
#include "battle.h"
#include "movement.h"
#include "combat.h"
#include "input.h"
#include "input_map.h"
#include "input_battle.h"
#include "interface.h"

#define S(s) s, sizeof(s) - 1

#define WINNER_NOBODY -1
#define WINNER_BATTLE -2

#define WORLD_DEFAULT "worlds/balkans"

static int battle_open(struct game *restrict game, struct region *restrict region)
{
	struct battle battle;
	unsigned char player;

	int status;

	if (battlefield_init(game, &battle, region) < 0) return -1;

	// Ask each player to position their pawns.
	// TODO ?this should be done on 2 stages (for region owners and for allies if there is an owner at the same location)
	for(player = 0; player < game->players_count; ++player)
	{
		if (!battle.players[player].pawns_count) continue; // skip players with no pawns

		switch (game->players[player].type)
		{
		case Neutral:
			continue;

		case Local:
			if (input_formation(game, region, &battle, player) < 0)
			{
				status = -1;
				goto finally;
			}
			break;
		}
	}
	// TODO formation for garrison

	while ((status = battle_end(game, &battle, game->players[region->owner].alliance)) < 0)
	{
		// Ask each player to perform battle actions.
		// TODO implement Computer and Remote
		for(player = 0; player < game->players_count; ++player)
		{
			if (!battle.players[player].pawns_count) continue; // skip players with no pawns

			switch (game->players[player].type)
			{
			case Neutral:
				continue;

			case Local:
				if (input_battle(game, &battle, player) < 0)
				{
					status = -1;
					goto finally;
				}
				break;
			}
		}

		// TODO shoot animation // TODO this should be part of player-specific input

		// TODO Deal damage to each pawn escaping from enemy pawns.
		battlefield_shoot(&battle);
		battlefield_clean_corpses(&battle);

		battlefield_movement_plan(game->players, game->players_count, battle.field, battle.pawns, battle.pawns_count);

		input_animation(game, &battle); // TODO this should be part of player-specific input

		battlefield_movement_perform(battle.field, battle.pawns, battle.pawns_count);

		// TODO fight animation // TODO this should be part of player-specific input

		battlefield_fight(game, &battle);
		battlefield_clean_corpses(&battle);
	}

	// TODO show battle overview // this is player-specific
	// winner team is status

finally:
	battlefield_term(game, &battle);
	return status;
}

static int play(struct game *restrict game)
{
	unsigned char player;
	struct region *region;

	size_t index;
	size_t i;

	struct troop *troop, *next;

	unsigned char alliance;
	uint16_t alliances; // this limits the alliance numbers to the number of bits

	unsigned char slots, owner_troop;

	struct resources income;
	struct resources expenses[PLAYERS_LIMIT];
	unsigned char alive[PLAYERS_LIMIT];

	do
	{
		memset(expenses, 0, sizeof(expenses));
		memset(alive, 0, sizeof(alive));

		// Ask each player to perform map actions.
		// TODO implement Computer and Remote
		for(player = 0; player < game->players_count; ++player)
			switch (game->players[player].type)
			{
			case Neutral:
				continue;

			case Local:
				if (input_map(game, player) < 0) return WINNER_NOBODY; // TODO
				break;
			}

		// Perform region-specific actions.

		for(index = 0; index < game->regions_count; ++index)
		{
			region = game->regions + index;

			for(troop = region->troops; troop; troop = troop->_next)
				resource_add(expenses + troop->owner, &troop->unit->expense);

			// Update training time and check if there are trained units.
			if (region->train[0] && (++region->train_time == region->train[0]->time))
			{
				if (troop_spawn(region, &region->troops, region->train[0], SLOT_UNITS, region->owner) < 0) ; // TODO

				region->train_time = 0;
				for(i = 1; i < TRAIN_QUEUE; ++i)
					region->train[i - 1] = region->train[i];
				region->train[TRAIN_QUEUE - 1] = 0;
			}

			// Update construction time and check if the building is finished.
			if ((region->construct >= 0) && (++region->build_progress == buildings[region->construct].time))
			{
				region->built |= (1 << region->construct);
				region->construct = -1;
				region->build_progress = 0;
			}

			// Move each troop for which movement is specified.
			troop = region->troops;
			while (troop)
			{
				next = troop->_next;
				if (troop->move != troop->location)
				{
					// Move the troop to its new location.
					troop_detach(&region->troops, troop);
					troop_attach(&troop->move->troops, troop);
				}
				troop = next;
			}
		}

		// TODO battle_assault()

		for(index = 0; index < game->regions_count; ++index)
		{
			signed char winner; // alliance of the new owner of the region (WINNER_BATTLE if it has to be determined by battle)
			winner = WINNER_NOBODY;

			region = game->regions + index;

			// If troops of two different alliances occupy the region, start a battle.
			for(troop = region->troops; troop; troop = troop->_next)
			{
				alliance = game->players[troop->owner].alliance;

				if (winner == WINNER_NOBODY) winner = alliance;
				else if (winner != alliance) winner = WINNER_BATTLE;
			}
			if (winner == WINNER_BATTLE) winner = battle_open(game, region);

			// Only troops of a single alliance are allowed to stay in the region.
			// If there are troops of more than one alliance, return any troop owned by enemy of the region's owner to its initial location.
			// If there are troops of just one alliance and the region's owner is not in it, change region's owner to the owner of a random troop.

			// TODO is it a good idea to choose owner based on number of troops?

			slots = 0;

			// Set the location of each troop and count the troops in the region.
			for(troop = region->troops; troop; troop = troop->_next)
			{
				// Remove dead troops.
				if (!troop->count)
				{
					troop_detach(&region->troops, troop);
					free(troop);
					continue;
				}

				if (game->players[troop->owner].alliance == winner)
				{
					// Set troop location to the current region.
					troop->location = region;
					slots += 1;
				}
				else
				{
					// Move the troop back to its original location.
					troop_detach(&region->troops, troop);
					troop_attach(&troop->location->troops, troop);
					troop->move = troop->location;
				}
			}

			if (winner != WINNER_NOBODY)
			{
				if (winner != game->players[region->owner].alliance)
				{
					// assert(slots);
					owner_troop = random() % slots;

					slots = 0;
					for(troop = region->troops; troop; troop = troop->_next)
					{
						if (slots == owner_troop)
						{
							// Set new region owner.
							region->owner = troop->owner;
							region->garrison.siege = 0;
							if (!region->garrison.troops) region->garrison.owner = troop->owner;
							break;
						}
						slots += 1;
					}

					// Cancel all constructions and trainings.
					region->construct = -1;
					region->build_progress = 0;
					for(i = 0; i < TRAIN_QUEUE; ++i) region->train[i] = 0;
					region->train_time = 0;
				}
			}

			// Add region income to the owner's treasury if the garrison is not under siege.
			if (region->owner == region->garrison.owner)
			{
				memset(&income, 0, sizeof(income));
				region_income(region, &income);
				resource_add(&game->players[region->owner].treasury, &income);
			}
			else // siege
			{
				const struct garrison_info *restrict garrison = garrison_info(region);

				// If the garrison has no more provisions, finish the siege.
				region->garrison.siege += 1;
				if (region->garrison.siege > garrison->provisions)
				{
					region->garrison.owner = region->owner;
					region->garrison.siege = 0;

					troop = region->garrison.troops;
					while (troop)
					{
						next = troop->_next;
						troop_detach(&region->garrison.troops, troop);
						free(troop);
						troop = next;
					}
				}
			}

			// Each player that controls a region is alive.
			alive[region->owner] = 1;
		}

		// Perform player-specific actions.
		alliances = 0;
		for(player = 0; player < game->players_count; ++player)
		{
			if (game->players[player].type == Neutral) continue;

			// Set player as dead, if not marked as alive.
			if (alive[player])
			{
				// Mark the alliance of each alive player as alive.
				alliances |= (1 << game->players[player].alliance);
			}
			else game->players[player].type = Neutral;

			// Subtract the player's expenses from the treasury.
			resource_spend(&game->players[player].treasury, expenses + player);
		}
	} while (alliances & (alliances - 1)); // while there is more than 1 alliance

	return alliances; // TODO convert this to alliance number
}

int main(int argc, char *argv[])
{
	const char *world;

	struct stat info;
	int file;
	char *buffer;
	union json *json;
	int success;

	struct game game;
	int winner;

	if (argc < 2) world = WORLD_DEFAULT;
	else world = argv[1];

	file = open(world, O_RDONLY);
	if (file < 0) return -1;
	if (fstat(file, &info) < 0)
	{
		close(file);
		return -1;
	}
	buffer = mmap(0, info.st_size, PROT_READ, MAP_SHARED, file, 0);
	close(file);
	if (buffer == MAP_FAILED) return -1;

	json = json_parse(buffer, info.st_size);
	munmap(buffer, info.st_size);

	if (!json)
	{
		write(2, S("Invalid map format\n"));
		return -1;
	}
	success = !world_init(json, &game);
	json_free(json);
	if (!success)
	{
		write(2, S("Invalid map data\n"));
		return -1;
	}

	srandom(time(0));
	if (!if_init(&game))
	{
		winner = play(&game);

		// TODO display game end message
		write(2, S("game finished\n"));
	}

	world_term(&game);

	return 0;
}
