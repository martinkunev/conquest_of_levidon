/*
 * Conquest of Levidon
 * Copyright (C) 2016  Martin Kunev <martinkunev@gmail.com>
 *
 * This file is part of Conquest of Levidon.
 *
 * Conquest of Levidon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 3 of the License.
 *
 * Conquest of Levidon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Conquest of Levidon.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "errors.h"
#include "game.h"
#include "draw.h"
#include "resources.h"
#include "map.h"
#include "world.h"
#include "pathfinding.h"
#include "movement.h"
#include "battle.h"
#include "combat.h"
#include "input_menu.h"
#include "input_map.h"
#include "input_battle.h"
#include "input_report.h"
#include "computer_map.h"
#include "computer_battle.h"
#include "interface.h"
#include "display_common.h"
#include "display_map.h"
#include "display_battle.h"
#include "menu.h"

#define S(s) s, sizeof(s) - 1

#define WINNER_NOBODY -1
#define WINNER_BATTLE -2

// WARNING: Player 0 and alliance 0 are hard-coded as neutral.

// TODO in some circumstances player 0 is used for objects without owner

/*
Invariants at the beginning of a turn:
- no enemies are located in the same (region, in_garrison)
- all troops in a garrison are owned by the player owning the garrison
- the owner of a garrison is the same as the owner of its region if there is no garrison built
- troops can only assault garrison in the region where they are located
- constructions and training are only possible when the region and its garrison have the same owner
*/

enum {ROUNDS_STALE_LIMIT_OPEN = 10, ROUNDS_STALE_LIMIT_ASSAULT = 20};

// Returns the number of the alliance that won the battle.
static int play_battle(struct game *restrict game, struct region *restrict region, int assault)
{
	unsigned player, alliance;

	unsigned round_activity_last;
	int winner = -1;

	struct battle battle;

	struct position (*movements)[MOVEMENT_STEPS + 1];

	if (battlefield_init(game, &battle, region, assault) < 0) return -1;

	movements = malloc(battle.pawns_count * sizeof(*movements));
	if (!movements)
	{
		battlefield_term(game, &battle);
		return ERROR_MEMORY;
	}

	battle.round = 0;

	// Ask each player to position their pawns.
	for(player = 0; player < game->players_count; ++player)
	{
		if (!battle.players[player].alive) continue;

		switch (game->players[player].type)
		{
		case Neutral:
			continue;

		case Computer:
			if (computer_formation(game, &battle, player) < 0)
				goto finally;
			break;

		case Local:
			if (input_formation(game, &battle, player) < 0)
				goto finally;
			break;
		}
	}

	battle.round = 1;
	round_activity_last = 1;

	while ((winner = battle_end(game, &battle)) < 0)
	{
		struct adjacency_list *graph[PLAYERS_LIMIT] = {0};
		struct obstacles *obstacles[PLAYERS_LIMIT] = {0};

		unsigned step;
		size_t i;

		obstacles[PLAYER_NEUTRAL] = path_obstacles_alloc(game, &battle, PLAYER_NEUTRAL);
		if (!obstacles[PLAYER_NEUTRAL]) abort();

		battlefield_index_build(&battle);

		// Ask each player to give commands to their pawns.
		for(player = 0; player < game->players_count; ++player)
		{
			if (!battle.players[player].alive) continue;

			alliance = game->players[player].alliance;

			if (!obstacles[alliance])
			{
				obstacles[alliance] = path_obstacles_alloc(game, &battle, player);
				if (!obstacles[alliance]) abort();
				graph[alliance] = visibility_graph_build(&battle, obstacles[alliance], 2); // 2 vertices for origin and target
				if (!graph[alliance]) abort();
			}

			switch (game->players[player].type)
			{
			case Neutral:
				continue;

			case Computer:
				if (computer_battle(game, &battle, player, graph[alliance], obstacles[alliance]) < 0)
					goto finally;
				break;

			case Local:
				if (input_battle(game, &battle, player, graph[alliance], obstacles[alliance]) < 0)
					goto finally;
				break;
			}
		}

		// Deal damage from shooters.
		input_animation_shoot(game, &battle); // TODO this should be part of player-specific input
		combat_ranged(&battle, obstacles[PLAYER_NEUTRAL]); // treat all gates as closed for shooting
		if (battlefield_clean(game, &battle)) round_activity_last = battle.round;

		// Perform pawn movement in steps.
		// Invariant: Before and after each step there are no overlapping pawns.
		for(step = 0; step < MOVEMENT_STEPS; ++step)
		{
			// Remember the position of each pawn because it is necessary for the movement animation.
			for(i = 0; i < battle.pawns_count; ++i)
				movements[i][step] = battle.pawns[i].position;

			// Plan the movement of each pawn.
			if (movement_plan(&battle, graph, obstacles) < 0)
				abort(); // TODO

			// Detect collisions caused by moving pawns and resolve them by modifying pawn movement.
			// Set final position of each pawn.
			if (movement_collisions_resolve(game, &battle, graph, obstacles) < 0)
				abort(); // TODO
		}
		// Remember the final position of each pawn because it is necessary for the movement animation.
		for(i = 0; i < battle.pawns_count; ++i)
			movements[i][step] = battle.pawns[i].position;

		input_animation(game, &battle, movements); // TODO this should be part of player-specific input

		// TODO fight animation // TODO this should be part of player-specific input
		combat_melee(game, &battle);
		if (battlefield_clean(game, &battle)) round_activity_last = battle.round;

		for(i = 0; i < PLAYERS_LIMIT; ++i)
		{
			free(obstacles[i]);
			visibility_graph_free(graph[i]);
		}

		// Cancel the battle if nothing is killed/destroyed for a certain number of rounds.
		if ((battle.round - round_activity_last) >= (assault ? ROUNDS_STALE_LIMIT_ASSAULT : ROUNDS_STALE_LIMIT_OPEN))
		{
			// Attacking troops retreat to the region they came from.
			for(i = 0; i < battle.pawns_count; ++i)
			{
				struct troop *restrict troop;

				if (!battle.pawns[i].count) continue;

				troop = battle.pawns[i].troop;
				if (game->players[troop->owner].alliance != battle.defender)
					troop->move = troop->location;
			}

			winner = battle.defender;
			break;
		}

		battle.round += 1;
	}

	input_report_battle(game, &battle); // TODO this is player-specific

finally:
	free(movements);
	battlefield_term(game, &battle);
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
		struct
		{
			enum {BATTLE_OPEN = 1, BATTLE_ASSAULT} type;
			unsigned char winner;
		} battle_info[REGIONS_LIMIT] = {0};

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
					else if (troop->move->owner != region->owner)
					{
						struct resources expense;
						resource_multiply(&expense, &troop->unit->income, 2);
						resource_add(expenses + troop->owner, &expense);
					}
					else resource_add(expenses + troop->owner, &troop->unit->income);
				}
			}
			else
			{
				// Troops expenses are covered by another region. Double expenses.
				for(troop = region->troops; troop; troop = troop->_next)
				{
					struct resources expense;
					if (troop->move == LOCATION_GARRISON) continue;
					resource_multiply(&expense, &troop->unit->income, 2);
					resource_add(expenses + troop->owner, &expense);
				}
			}

			region_orders_process(region);

			// Move troops in and out of garrison and put them in their target regions.
			// New troop locations will be set after all battles have concluded.
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
			unsigned alliances_assault = 0, alliances_open = 0;
			int manual_assault = 0, manual_open = 0;

			region = game->regions + index;

			// Start open battle if troops of two different alliances occupy the region.
			// If there is no open battle and there are troops preparing for assault, start assault battle.
			for(troop = region->troops; troop; troop = troop->_next)
			{
				if (troop->move == LOCATION_GARRISON)
				{
					alliances_assault |= (1 << game->players[troop->owner].alliance);
					if (game->players[troop->owner].type == Local) manual_assault = 1;
				}
				else
				{
					alliances_open |= (1 << game->players[troop->owner].alliance);
					if (game->players[troop->owner].type == Local) manual_open = 1;
				}
			}
			if (alliances_open & (alliances_open - 1))
			{
				int status = (manual_open ? play_battle(game, region, 0) : calculate_battle(game, region, 0));
				if (status < 0) return status;

				battle_info[index].winner = status;
				battle_info[index].type = BATTLE_OPEN;
			}
			else if (alliances_assault & (alliances_assault - 1))
			{
				int status = (manual_assault ? play_battle(game, region, 1) : calculate_battle(game, region, 1));
				if (status < 0) return status;

				battle_info[index].winner = status;
				battle_info[index].type = BATTLE_ASSAULT;
			}
			else battle_info[index].type = 0;
		}

		// Perform post-battle cleanup actions.
		for(index = 0; index < game->regions_count; ++index)
		{
			unsigned region_owner_old;

			region = game->regions + index;
			region_owner_old = region->owner;

			if (battle_info[index].type)
				region_battle_cleanup(game, region, ((battle_info[index].type == BATTLE_OPEN) ? 0 : 1), battle_info[index].winner);

			region_turn_process(game, region);

			// Cancel all constructions and trainings if region owner changed.
			if (region->owner != region_owner_old)
				region_orders_cancel(region);

			// Each player controlling a region or a garrison is alive.
			alive[region->owner] = 1;
			alive[region->garrison.owner] = 1;
		}

		// Adjust troop locations and calculate region income.
		for(index = 0; index < game->regions_count; ++index)
		{
			region = game->regions + index;

			for(troop = region->troops; troop; troop = next)
			{
				next = troop->_next;

				if (troop->location == LOCATION_GARRISON) continue;

				// Update troop location.
				// Return retreating troops to their previous location.
				if (troop->move == region) troop->location = region;
				else
				{
					troop_detach(&region->troops, troop);
					if (allies(game, troop->owner, troop->location->owner))
						troop_attach(&troop->location->troops, troop);
					else
						free(troop); // the troop has no region to return to; kill it
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

	return alliances; // TODO ? convert this to alliance number
}

int main(int argc, char *argv[])
{
	struct game game;
	int status;

	srandom(time(0));

	menu_init();

	if (if_init() < 0)
		return 1;
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
