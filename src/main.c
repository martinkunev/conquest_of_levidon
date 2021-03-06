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

#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

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
#include "input_battle.h"
#include "input_report.h"
#include "computer_battle.h"
#include "interface.h"
#include "display_common.h"
#include "display_map.h"
#include "menu.h"
#include "players.h"

#define S(s) s, sizeof(s) - 1

#define WINNER_NOBODY -1
#define WINNER_BATTLE -2

// WARNING: Player 0 is hard-coded as neutral.

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
static int play_battle(struct game *restrict game, struct region *restrict region, enum battle_type battle_type)
{
	unsigned round_activity_last;
	int winner = -1;

	struct battle battle;

	struct position (*movements)[MOVEMENT_STEPS + 1];

	unsigned players_local = 0;

	int status;

	if (battlefield_init(game, &battle, region, battle_type) < 0)
		return -1;

	if (game->players_local_count >= 2)
	{
		struct state_report state;
		state.title = REPORT_TITLE_BATTLE;
		state.title_size = sizeof(REPORT_TITLE_BATTLE) - 1;
		for(size_t player = 0; player < game->players_count; player += 1)
			if ((game->players[player].type == Local) && (battle.players[player].state == PLAYER_ALIVE))
				state.players[players_local++] = player;
		state.players_count = players_local;
		input_report_players(&state);
	}

	movements = malloc(battle.pawns_count * sizeof(*movements));
	if (!movements)
	{
		battlefield_term(game, &battle);
		return ERROR_MEMORY;
	}

	battle.round = 0;

	// Ask each player to position their pawns.
	status = players_formation(game, &battle, players_local >= 2);
	if (status < 0)
		goto finally;

	battle.round = 1;
	round_activity_last = 1;

	while ((winner = battle_end(game, &battle)) < 0)
	{
		const struct obstacles *obstacles[PLAYERS_LIMIT] = {0};
		struct adjacency_list *graph[PLAYERS_LIMIT] = {0};

		unsigned step;
		size_t i;

		unsigned char alliance_neutral = game->players[PLAYER_NEUTRAL].alliance;

		// TODO if there are no local players, resolve the battle automatically

		obstacles[alliance_neutral] = path_obstacles_alloc(game, &battle, PLAYER_NEUTRAL);
		if (!obstacles[alliance_neutral]) abort();
		graph[alliance_neutral] = visibility_graph_build(&battle, obstacles[alliance_neutral], 2); // 2 vertices for origin and target
		if (!graph[alliance_neutral]) abort();

		battlefield_index_build(&battle);

		for(size_t player = 0; player < game->players_count; ++player)
		{
			size_t alliance = game->players[player].alliance;

			if (!obstacles[alliance])
			{
				obstacles[alliance] = path_obstacles_alloc(game, &battle, player);
				if (!obstacles[alliance]) abort();
			}
			graph[player] = visibility_graph_build(&battle, obstacles[alliance], 2); // 2 vertices for origin and target
			if (!graph[player])
				abort();
		}

		// Ask each player to give commands to their pawns.
		status = players_battle(game, &battle, obstacles, graph);
		if (status < 0)
			goto finally;

		// Deal damage from shooters.
		input_animation_shoot(game, &battle);
		combat_ranged(&battle, obstacles[alliance_neutral]); // treat all gates as closed for shooting
		if (battlefield_clean(game, &battle)) round_activity_last = battle.round;

		// Perform pawn movement in steps.
		// Invariant: Before and after each step there are no overlapping pawns.
		for(step = 0; step < MOVEMENT_STEPS; ++step)
		{
			// TODO open a gate if a pawn passes through it; close it at the end of the round

			// Remember the position of each pawn because it is necessary for the movement animation.
			for(i = 0; i < battle.pawns_count; ++i)
				movements[i][step] = battle.pawns[i].position;

			// Plan the movement of each pawn.
			if (movement_plan(game, &battle, graph, obstacles) < 0)
				abort(); // TODO

			// Detect collisions caused by moving pawns and resolve them by modifying pawn movement.
			// Set final position of each pawn.
			if (movement_collisions_resolve(game, &battle) < 0)
				abort(); // TODO
		}
		// Remember the final position of each pawn because it is necessary for the movement animation.
		for(i = 0; i < battle.pawns_count; ++i)
			movements[i][step] = battle.pawns[i].position;

		input_animation_move(game, &battle, movements);

		// TODO input_animation_fight()
		combat_melee(game, &battle);
		if (battlefield_clean(game, &battle)) round_activity_last = battle.round;

		for(i = 0; i < PLAYERS_LIMIT; ++i)
		{
			free((void *)obstacles[i]); // TODO fix this cast
			visibility_graph_free(graph[i]);
		}

		// Cancel the battle if nothing is killed/destroyed for a certain number of rounds.
		if ((battle.round - round_activity_last) >= ((battle_type == BATTLE_ASSAULT) ? ROUNDS_STALE_LIMIT_ASSAULT : ROUNDS_STALE_LIMIT_OPEN))
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

	input_report_battle(game, &battle);

finally:
	free(movements);
	battlefield_term(game, &battle);
	return winner;
}

// Returns whether there is a winner. On error, returns error code.
static int play(struct game *restrict game)
{
	unsigned char player;
	struct region *region;
	struct troop *troop, *next;

	size_t index;

	uint16_t alliances; // this limits the alliance numbers to the number of bits

	int status;

	status = players_init(game);
	if (status < 0)
		return status;

	do
	{
		struct resources expenses[PLAYERS_LIMIT] = {0};
		unsigned char alive[PLAYERS_LIMIT] = {0};
		struct
		{
			enum battle_type type;
			unsigned char winner;
		} battle_info[REGIONS_LIMIT] = {0};

		// Ask each player to perform map actions.
		status = players_map(game);
		if (status < 0)
			goto finally;

		// Perform region-specific actions.
		for(index = 0; index < game->regions_count; ++index)
		{
			struct resources expense;

			region = game->regions + index;

			// TODO improve the code below

			for(troop = region->troops; troop; troop = next)
			{
				next = troop->_next;
				if (troop->dismiss)
				{
					troop_detach(&region->troops, troop);
					free(troop);
				}
			}

			// Calculate region expenses.
			if (region->owner == region->garrison.owner)
			{
				// Troops expenses are covered by current region.
				for(troop = region->troops; troop; troop = troop->_next)
				{
					if (troop->move == LOCATION_GARRISON)
						continue;

					if (troop->move->owner != region->owner)
						resource_multiply(&expense, &troop->unit->support, 2 * troop->count);
					else
						resource_multiply(&expense, &troop->unit->support, troop->count);
					resource_add(expenses + troop->owner, &expense);
				}
			}
			else
			{
				// Troops expenses are covered by another region. Double expenses.
				for(troop = region->troops; troop; troop = troop->_next)
				{
					if ((troop->move == LOCATION_GARRISON) && (troop->owner == region->garrison.owner))
						continue; // sieged troop

					resource_multiply(&expense, &troop->unit->support, 2 * troop->count);
					resource_add(expenses + troop->owner, &expense);
				}
			}
			expenses[region->owner].gold -= 10 * sqrt(region->population / 1000.0); // region governing
			for(size_t i = 0; i < BUILDINGS_COUNT; ++i)
				if (region->built & (1 << i))
					resource_add(expenses + region->owner, &BUILDINGS[i].support);

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
			uint32_t alliances_assault = 0, alliances_open = 0, alliances;
			int manual_assault = 0, manual_open = 0;

			int status;

			region = game->regions + index;

			// Collect information about the troops in each region.
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

			// Check if the owner of the garrison has troops in the garrison and wants to reinforce the defense.
			alliances = alliances_open | alliances_assault;
			if ((alliances & (alliances - 1)) && (alliances_assault & (1 << game->players[region->garrison.owner].alliance)))
			{
				region->garrison.reinforce = false;
				status = players_invasion(game, region);
				if (status < 0)
					goto finally;

				if (region->garrison.reinforce)
				{
					if (game->players[region->garrison.owner].type == Local) manual_open = 1;
					alliances_open |= (1 << game->players[region->garrison.owner].alliance);
					battle_info[index].type = BATTLE_OPEN_REINFORCED;
				}
			}

			// Start open battle if troops of two different alliances occupy the region.
			// If there is no open battle and there are troops preparing for assault, start assault battle.
			if (alliances_open & (alliances_open - 1))
			{
				if (!battle_info[index].type)
					battle_info[index].type = BATTLE_OPEN;

				status = (manual_open ? play_battle(game, region, battle_info[index].type) : calculate_battle(game, region, 0));
				if (status < 0) goto finally;

				battle_info[index].winner = status;
			}
			else if (alliances_assault & (alliances_assault - 1))
			{
				battle_info[index].type = BATTLE_ASSAULT;

				status = (manual_assault ? play_battle(game, region, battle_info[index].type) : calculate_battle(game, region, 1));
				if (status < 0) goto finally;

				battle_info[index].winner = status;
			}
			else battle_info[index].type = BATTLE_NONE;
		}

		// Perform post-battle cleanup actions.
		for(index = 0; index < game->regions_count; ++index)
		{
			unsigned region_owner_old;

			region = game->regions + index;
			region_owner_old = region->owner;

			if (battle_info[index].type)
				region_battle_cleanup(game, region, (battle_info[index].type == BATTLE_ASSAULT), battle_info[index].winner);

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
				region_production(region, &income);
				resource_add(&game->players[region->owner].treasury, &income);
			}
		}

		for(index = 0; index < game->regions_count; ++index)
			region_troops_merge(game->regions + index);

		// Perform player-specific actions.
		alliances = 0;
		game->players_local_count = 0;
		for(player = 0; player < game->players_count; ++player)
		{
			// Set player as dead, if not marked as alive.
			if (!alive[player])
				game->players[player].type = Neutral;

			switch (game->players[player].type)
			{
			case Neutral:
				continue;

			case Local:
				game->players_local[game->players_local_count++] = player;
				break;
			}

			// Mark the alliance of each alive player as alive.
			alliances |= (1 << game->players[player].alliance);

			// Adjust player treasury for the income and expenses.
			resource_spend(&game->players[player].treasury, expenses + player);
		}

		game->turn += 1;

		if (!game->players_local_count) // no more human-controlled players
		{
			status = 0;
			goto finally;
		}
	} while (1 || (alliances & (alliances - 1))); // while there is more than 1 alliance

	status = alliances;

finally:
	players_term(game);
	return status;
}

int main(int argc, char *argv[])
{
	struct game game;
	int status;

	status = sigaction(SIGPIPE, &(struct sigaction){.sa_handler = SIG_IGN}, 0);
	assert(!status);

	srandom(time(0));

	assert(PLAYERS_LIMIT <= 16); // TODO this should be compile-time assert

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
		if (status >= 0) input_report_map(&game);

		if_storage_term();
		world_unload(&game);

		if (status == ERROR_CANCEL) continue;
		else if (status < 0) return status;
	}

	if_term();

	menu_term();

	return 0;
}
