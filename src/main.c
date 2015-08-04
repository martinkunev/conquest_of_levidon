#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "base.h"
#include "map.h"
#include "world.h"
#include "pathfinding.h"
#include "battle.h"
#include "movement.h"
#include "combat.h"
#include "input.h"
#include "input_menu.h"
#include "input_map.h"
#include "input_battle.h"
#include "input_report.h"
#include "computer.h"
#include "interface.h"
#include "display.h"
#include "menu.h"

#define S(s) s, sizeof(s) - 1

#define WINNER_NOBODY -1
#define WINNER_BATTLE -2

// Returns the number of the alliance that won the battle.
static int play_battle(struct game *restrict game, struct battle *restrict battle, unsigned char defender)
{
	unsigned player, alliance;

	int status;
	int winner;

	size_t i;

	battle->round = 0;

	// Ask each player to position their pawns.
	for(player = 0; player < game->players_count; ++player)
	{
		if (!battle->players[player].alive) continue;

		switch (game->players[player].type)
		{
		case Neutral:
			continue;

		case Computer:
			if (computer_formation(game, battle, player) < 0)
				return -1;
			break;

		case Local:
			if (input_formation(game, battle, player) < 0)
				return -1;
			break;
		}
	}

	battle->round = 1;

	while ((winner = battle_end(game, battle, defender)) < 0)
	{
		struct adjacency_list *graph[PLAYERS_LIMIT] = {0};
		struct obstacles *obstacles[PLAYERS_LIMIT] = {0};

		// Ask each player to give commands to their pawns.
		for(player = 0; player < game->players_count; ++player)
		{
			if (!battle->players[player].alive) continue;

			alliance = game->players[player].alliance;

			if (!obstacles[alliance])
			{
				obstacles[alliance] = path_obstacles(game, battle, player);
				if (!obstacles[alliance]) abort();
				graph[alliance] = visibility_graph_build(battle, obstacles[alliance]);
				if (!graph[alliance]) abort();
			}

			switch (game->players[player].type)
			{
			case Neutral:
				continue;

			case Computer:
				if (computer_battle(game, battle, player) < 0)
					return -1;
				break;

			case Local:
				if (input_battle(game, battle, player, graph[alliance], obstacles[alliance]) < 0)
					return -1;
				break;
			}
		}

		// TODO ?Deal damage to each pawn escaping from enemy pawns.

		// TODO shoot animation // TODO this should be part of player-specific input
		battle_shoot(battle, obstacles[PLAYER_NEUTRAL]); // treat all gates as closed for shooting
		battlefield_clean(battle);

		for(i = 0; i < battle->pawns_count; ++i)
		{
			if (!battle->pawns[i].count) continue;
			status = movement_attack_plan(battle->pawns + i, graph[alliance], obstacles[alliance]);
			if (status < 0) abort(); // TODO
		}

		battlefield_movement_plan(game->players, game->players_count, battle->field, battle->pawns, battle->pawns_count);

		input_animation(game, battle); // TODO this should be part of player-specific input

		for(i = 0; i < battle->pawns_count; ++i)
		{
			if (!battle->pawns[i].count) continue;
			status = battlefield_movement_perform(battle, battle->pawns + i, graph[alliance], obstacles[alliance]);
			if (status < 0) abort(); // TODO
		}

		// TODO fight animation // TODO this should be part of player-specific input
		battle_fight(game, battle);
		battlefield_clean(battle);

		battle->round += 1;

		for(i = 0; i < PLAYERS_LIMIT; ++i)
		{
			free(obstacles[i]);
			visibility_graph_free(graph[i]);
		}
	}

	return winner;
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

			case Computer: // TODO implement this
			case Local:
				if (input_map(game, player) < 0) return WINNER_NOBODY; // TODO
				break;
			}

		// Perform region-specific actions.

		for(index = 0; index < game->regions_count; ++index)
		{
			region = game->regions + index;

			if (region->owner == region->garrison.owner)
			{
				// Troops are supported by current region.
				for(troop = region->troops; troop; troop = troop->_next)
					resource_add(expenses + troop->owner, &troop->unit->expense);
			}
			else
			{
				// Troops are supported by another region. Double expenses.
				for(troop = region->troops; troop; troop = troop->_next)
				{
					struct resources expense;
					resource_multiply(&expense, &troop->unit->expense, 2);
					resource_add(expenses + troop->owner, &expense);
				}
			}

			map_train(region);
			map_build(region);

			// Move each troop for which movement is specified.
			for(troop = region->troops; troop; troop = next)
			{
				next = troop->_next;
				if (troop->move == troop->location) continue;

				if (troop->move == LOCATION_GARRISON)
				{
					// Move the troop to the garrison.
					troop->location = LOCATION_GARRISON;
					troop_detach(&region->troops, troop);
					troop_attach(&region->garrison.troops, troop);
				}
				else
				{
					// Move the troop to the specified region.
					troop_detach(&region->troops, troop);
					troop_attach(&troop->move->troops, troop);
				}
			}
			for(troop = region->garrison.troops; troop; troop = next)
			{
				next = troop->_next;
				if (troop->move == troop->location) continue;

				// Move the troop out of the garrison.
				troop->location = region;
				troop_detach(&region->garrison.troops, troop);
				troop_attach(&troop->move->troops, troop);
			}
		}

		for(index = 0; index < game->regions_count; ++index)
		{
			int assault = 0;

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
			if (winner == WINNER_BATTLE) // open battle
			{
				struct battle battle;
				if (battlefield_init(game, &battle, region, 0) < 0) abort(); // TODO
				winner = play_battle(game, &battle, game->players[battle.region->owner].alliance);
				input_report(game, &battle); // TODO this is player-specific
				battlefield_term(game, &battle);
			}
			else if (region->garrison.assault) // assault
			{
				struct battle battle;
				if (battlefield_init(game, &battle, region, 1) < 0) abort(); // TODO
				winner = play_battle(game, &battle, game->players[battle.region->garrison.owner].alliance);
				input_report(game, &battle); // TODO this is player-specific
				battlefield_term(game, &battle);

				assault = 1;
			}
			region->garrison.assault = 0;

			// Only troops of a single alliance are allowed to stay in the region.
			// If there are troops of more than one alliance, return any troop owned by enemy of the region's owner to its initial location.
			// If there are troops of just one alliance and the region's owner is not in it, change region's owner to the owner of a random troop.

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
					troop->location = troop->move;
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
			for(troop = region->garrison.troops; troop; troop = troop->_next)
			{
				// Remove dead troops.
				if (!troop->count)
				{
					troop_detach(&region->garrison.troops, troop);
					free(troop);
				}
			}

			if (winner != WINNER_NOBODY)
			{
				if (assault)
				{
					if (winner == game->players[region->garrison.owner].alliance)
					{
						if (!region->troops)
						{
							region->garrison.siege = 0;
							region->owner = region->garrison.owner;
						}
					}
					else
					{
						region->garrison.owner = region->owner;
						region->garrison.siege = 0;
					}
				}
				else if (winner != game->players[region->owner].alliance)
				{
					if (winner == game->players[region->garrison.owner].alliance)
					{
						region->owner = region->garrison.owner;
					}
					else
					{
						// assert(slots);
						owner_troop = random() % slots;
						for(troop = region->troops; troop; troop = troop->_next)
						{
							if (owner_troop) owner_troop -= 1;
							else
							{
								region->owner = troop->owner;
								if (!region->garrison.troops) region->garrison.owner = troop->owner;
								break;
							}
						}
					}

					region->garrison.siege = 0;

					// Cancel all constructions and trainings.
					region->construct = -1;
					region->build_progress = 0;
					for(i = 0; i < TRAIN_QUEUE; ++i) region->train[i] = 0;
					region->train_progress = 0;
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

				// If the garrison has no more troops or no more provisions, finish the siege.
				if (!region->garrison.troops || (++region->garrison.siege > garrison->provisions))
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

		game->turn += 1;
	} while (alliances & (alliances - 1)); // while there is more than 1 alliance

	return alliances; // TODO convert this to alliance number
}

int main(int argc, char *argv[])
{
	struct game game;
	int winner;
	int status;

	srandom(time(0));

	menu_init();

	if_init();
	if_load_images();

	if_display();

	status = input_load(&game);
	if (status < 0)
	{
		// TODO
		return -1;
	}

	// Initialize region input recognition.
	if_storage_init(&game, MAP_WIDTH, MAP_HEIGHT);

	if_display();

	winner = play(&game);

	// TODO display game end message
	write(2, S("game finished\n"));

	if_storage_term();
	if_term();

	world_unload(&game);

	menu_term();

	return 0;
}
