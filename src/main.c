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
#include "display_common.h"
#include "display_map.h"
#include "display_battle.h"
#include "menu.h"

#define S(s) s, sizeof(s) - 1

#define WINNER_NOBODY -1
#define WINNER_BATTLE -2

// WARNING: Player 0 and alliance 0 are hard-coded as neutral.

/*
Invariants at the beginning of a turn:
- no enemies are located in the same (region, in_garrison)
- all troops in a garrison are owned by the player owning the garrison
- the owner of a garrison is the same as the owner of its region if there is no garrison built
- troops can only assault garrison in the region where they are located
- constructions and training are only possible when the region and its garrison have the same owner
*/

enum {ROUNDS_STALE_LIMIT = 10};

// Returns the number of the alliance that won the battle.
static int play_battle(struct game *restrict game, struct battle *restrict battle, unsigned char defender)
{
	unsigned player, alliance;

	int status;
	int winner;

	size_t i;

	unsigned round_activity_last;

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
	round_activity_last = 1;

	while ((winner = battle_end(game, battle, defender)) < 0)
	{
		struct adjacency_list *graph[PLAYERS_LIMIT] = {0};
		struct obstacles *obstacles[PLAYERS_LIMIT] = {0};

		obstacles[PLAYER_NEUTRAL] = path_obstacles(game, battle, PLAYER_NEUTRAL);
		if (!obstacles[PLAYER_NEUTRAL]) abort();

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
				if (computer_battle(game, battle, player, graph[alliance], obstacles[alliance]) < 0)
					return -1;
				break;

			case Local:
				if (input_battle(game, battle, player, graph[alliance], obstacles[alliance]) < 0)
					return -1;
				break;
			}
		}

		// TODO ?Deal damage to each pawn escaping from enemy pawns.

		input_animation_shoot(game, battle); // TODO this should be part of player-specific input
		battle_shoot(battle, obstacles[PLAYER_NEUTRAL]); // treat all gates as closed for shooting
		if (battlefield_clean(battle)) round_activity_last = battle->round;

		for(i = 0; i < battle->pawns_count; ++i)
		{
			if (!battle->pawns[i].count) continue;
			alliance = game->players[battle->pawns[i].troop->owner].alliance;
			status = movement_attack_plan(battle->pawns + i, graph[alliance], obstacles[alliance]);
			if (status < 0) abort(); // TODO
		}

		battlefield_movement_plan(game->players, game->players_count, battle->field, battle->pawns, battle->pawns_count);

		input_animation(game, battle); // TODO this should be part of player-specific input

		// TODO ugly; there should be a way to unify these
		for(i = 0; i < battle->pawns_count; ++i)
		{
			if (!battle->pawns[i].count) continue;
			alliance = game->players[battle->pawns[i].troop->owner].alliance;
			status = battlefield_movement_perform(battle, battle->pawns + i, graph[alliance], obstacles[alliance]);
			if (status < 0) abort(); // TODO
		}
		for(i = 0; i < battle->pawns_count; ++i)
		{
			if (!battle->pawns[i].count) continue;
			alliance = game->players[battle->pawns[i].troop->owner].alliance;
			status = battlefield_movement_attack(battle, battle->pawns + i, graph[alliance], obstacles[alliance]);
			if (status < 0) abort(); // TODO
		}

		// TODO fight animation // TODO this should be part of player-specific input
		battle_fight(game, battle);
		if (battlefield_clean(battle)) round_activity_last = battle->round;

		for(i = 0; i < PLAYERS_LIMIT; ++i)
		{
			free(obstacles[i]);
			visibility_graph_free(graph[i]);
		}

		// Cancel the battle if nothing is killed/destroyed for a certain number of rounds.
		if ((battle->round - round_activity_last) >= ROUNDS_STALE_LIMIT)
			return defender;

		battle->round += 1;
	}

	input_report_battle(game, battle); // TODO this is player-specific

	return winner;
}

static int play(struct game *restrict game)
{
	unsigned char player;
	struct region *region;
	struct troop *troop, *next;

	size_t index;

	uint16_t alliances; // this limits the alliance numbers to the number of bits

	int status;

	do
	{
		struct resources expenses[PLAYERS_LIMIT] = {0};
		unsigned char alive[PLAYERS_LIMIT] = {0};

		// Ask each player to perform map actions.
		for(player = 0; player < game->players_count; ++player)
			switch (game->players[player].type)
			{
			case Neutral:
				continue;

			case Local:
				status = input_map(game, player);
				if (status < 0) return status;
				break;

			case Computer:
				status = computer_map(game, player);
				if (status < 0) return status;
				break;
			}

		// Perform region-specific actions.
		for(index = 0; index < game->regions_count; ++index)
		{
			region = game->regions + index;

			// Calculate region expenses.
			if (region->owner == region->garrison.owner)
			{
				// Troops expenses are covered by current region.
				for(troop = region->troops; troop; troop = troop->_next)
				{
					if (troop->move == LOCATION_GARRISON) continue;
					resource_add(expenses + troop->owner, &troop->unit->expense);
				}
			}
			else
			{
				// Troops expenses are covered by another region. Double expenses.
				for(troop = region->troops; troop; troop = troop->_next)
				{
					struct resources expense;
					if (troop->move == LOCATION_GARRISON) continue;
					resource_multiply(&expense, &troop->unit->expense, 2);
					resource_add(expenses + troop->owner, &expense);
				}
			}

			region_orders_process(region);

			// Move troops in and out of garrison and put them in their target regions.
			// New troop location will be set after all battles have concluded.
			for(troop = region->troops; troop; troop = next)
			{
				next = troop->_next;
				if (troop->move == troop->location) continue;

				if (troop->move == LOCATION_GARRISON)
				{
					// Move troop to the garrison unless it prepares for assault.
					if (troop->owner == region->garrison.owner)
						troop->location = LOCATION_GARRISON;
				}
				else
				{
					if (troop->location == LOCATION_GARRISON) troop->location = region;

					if (troop->move != troop->location)
					{
						// Put the troop in the specified region.
						troop_detach(&region->troops, troop);
						troop_attach(&troop->move->troops, troop);
					}
				}
			}
		}

		// Settle conflicts by battles.
		for(index = 0; index < game->regions_count; ++index)
		{
			int assault = 0;
			int winner_alliance;

			unsigned region_owner_old = region->owner;

			region = game->regions + index;

			// Start open battle if troops of two different alliances occupy the region.
			// If there is no open battle and there are troops preparing for assault, start assault battle.
			for(troop = region->troops; troop; troop = troop->_next)
			{
				if (troop->move == LOCATION_GARRISON)
				{
					if (troop->owner != region->garrison.owner) assault = 1;
				}
				else if (!allies(game, troop->owner, region->owner))
				{
					struct battle battle;
					if (battlefield_init(game, &battle, region, 0) < 0) abort(); // TODO
					winner_alliance = play_battle(game, &battle, game->players[battle.region->owner].alliance);
					battlefield_term(game, &battle);
					if (winner_alliance < 0) return winner_alliance;

					region_battle_cleanup(game, region, 0, winner_alliance);

					goto after;
				}
			}
			if (assault)
			{
				struct battle battle;
				if (battlefield_init(game, &battle, region, 1) < 0) abort(); // TODO
				winner_alliance = play_battle(game, &battle, game->players[battle.region->garrison.owner].alliance);
				battlefield_term(game, &battle);
				if (winner_alliance < 0) return winner_alliance;

				region_battle_cleanup(game, region, 1, winner_alliance);
			}

after:
			region_turn_process(game, region);

			// Cancel all constructions and trainings if region owner changed.
			if (region->owner != region_owner_old)
				region_orders_cancel(region);

			// Each player that controls a region or a garrison is alive.
			alive[region->owner] = 1;
			alive[region->garrison.owner] = 1;
		}

		// Adjust troop locations and calculate region-specific income and expenses.
		for(index = 0; index < game->regions_count; ++index)
		{
			region = game->regions + index;

			for(troop = region->troops; troop; troop = next)
			{
				next = troop->_next;

				if (troop->location == LOCATION_GARRISON) continue;

				// Update troop location.
				// Return troops owned by enemy of the owner to their previous location.
				if (allies(game, troop->owner, region->owner)) troop->location = troop->move;
				else
				{
					troop_detach(&region->troops, troop);
					if (allies(game, troop->owner, troop->location->owner))
					{
						troop_attach(&troop->location->troops, troop);
						troop->move = troop->location;
					}
					else free(troop); // the troop has no region to return to; kill it
				}
			}

			// Add region income to the owner's treasury if the garrison is not under siege.
			if (region->owner == region->garrison.owner)
			{
				struct resources income = {0};
				region_income(region, &income);
				resource_add(&game->players[region->owner].treasury, &income);
			}
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

			// Adjust player treasury for the income and expenses.
			resource_spend(&game->players[player].treasury, expenses + player);
		}

		game->turn += 1;
	} while (alliances & (alliances - 1)); // while there is more than 1 alliance

	return alliances; // TODO convert this to alliance number
}

int main(int argc, char *argv[])
{
	struct game game;
	int status;

	srandom(time(0));

	menu_init();

	if_init();
	if_load_images();

	if_display();

	while (1)
	{
		status = input_load(&game);
		if (status < 0) return -1; // TODO

		// Initialize region input recognition.
		if_storage_init(&game, MAP_WIDTH, MAP_HEIGHT);
		if_display();

		status = play(&game);
		if (status > 0) input_report_map(&game);

		if_storage_term();
		world_unload(&game);

		if (status == ERROR_CANCEL) continue;
		else if (status < 0) return status;
	}

	if_term();

	menu_term();

	return 0;
}
