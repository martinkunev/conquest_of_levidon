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
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "log.h"
#include "game.h"
#include "draw.h"
#include "map.h"
#include "pathfinding.h"
#include "movement.h"
#include "battle.h"

#define PAWNS_LIMIT 12

#define FORMATION_RADIUS_ATTACK 3
#define FORMATION_RADIUS_DEFEND 4
#define FORMATION_RADIUS_FREE 10

const double formation_position_defend[2] = {12.5, 12.5};
const double formation_position_attack[NEIGHBORS_LIMIT][2] = {{25.0, 12.5}, {21.5, 3.5}, {12.5, 0.0}, {3.5, 3.5}, {0.0, 12.5}, {3.5, 21.5}, {12.5, 25.0}, {21.5, 21.5}};
const double formation_position_garrison[2] = {12.5, 0};
const double formation_position_assault[ASSAULT_LIMIT][2] = {{12.5, 20.0}, {0.0, 7.5}, {25.0, 7.5}, {3.5, 16.5}, {21.5, 16.5}};

static inline double distance(const double origin[static restrict 2], const double target[static restrict 2])
{
	double dx = target[0] - origin[0], dy = target[1] - origin[1];
	return sqrt(dx * dx + dy * dy);
}

// Returns whether the troop can be placed at the given field.
size_t formation_reachable_open(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct tile reachable[REACHABLE_LIMIT])
{
	size_t x, y;

	size_t reachable_count = 0;

	if (pawn->startup == NEIGHBOR_SELF)
	{
		if (allies(game, battle->region->owner, pawn->troop->owner))
		{
			for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
				for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
				{
					if (battle->field[y][x].blockage) continue;
					if (distance(formation_position_defend, (double [2]){x + 0.5, y + 0.5}) <= FORMATION_RADIUS_DEFEND)
						reachable[reachable_count++] = (struct tile){x, y};
				}
		}
		else // the pawn attacks from the garrison
		{
			for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
				for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
				{
					if (battle->field[y][x].blockage) continue;
					if (distance(formation_position_defend, (double [2]){x + 0.5, y + 0.5}) <= FORMATION_RADIUS_DEFEND) continue;
					if (distance(formation_position_defend, (double [2]){x + 0.5, y + 0.5}) < FORMATION_RADIUS_FREE)
						reachable[reachable_count++] = (struct tile){x, y};
				}
		}
	}
	else
	{
		for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
			for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
			{
				double target[2] = {x + 0.5, y + 0.5};
				if (battle->field[y][x].blockage) continue;
				if (distance(formation_position_defend, target) < FORMATION_RADIUS_FREE) continue;
				if (distance(formation_position_attack[pawn->startup], target) <= FORMATION_RADIUS_ATTACK)
					reachable[reachable_count++] = (struct tile){x, y};
			}
	}

	return reachable_count;
}

size_t formation_reachable_assault(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct tile reachable[REACHABLE_LIMIT])
{
	size_t x, y;

	size_t reachable_count = 0;

	if (pawn->startup == NEIGHBOR_GARRISON)
	{
		for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
			for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
			{
				if (battle->field[y][x].blockage) continue;
				if (distance(formation_position_garrison, (double [2]){x + 0.5, y + 0.5}) <= FORMATION_RADIUS_DEFEND)
					reachable[reachable_count++] = (struct tile){x, y};
			}
	}
	else
	{
		for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
			for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
			{
				if (battle->field[y][x].blockage) continue;
				if (distance(formation_position_assault[pawn->startup], (double [2]){x + 0.5, y + 0.5}) <= FORMATION_RADIUS_ATTACK)
					reachable[reachable_count++] = (struct tile){x, y};
			}
	}

	return reachable_count;
}

// Returns whether a pawn owned by the given player can pass through the field.
int battlefield_passable(const struct battlefield *restrict field, unsigned player)
{
	if (!field->blockage)
		return 1;
	if ((field->blockage == BLOCKAGE_GATE) && (field->owner == player))
		return 1;
	return 0;
}

// TODO revise this function
static void battlefield_init_open(const struct game *restrict game, struct battle *restrict battle)
{
	unsigned players_count = 0;
	signed char locations[PLAYERS_LIMIT]; // startup locations of the players participating in the battle
	signed char players[NEIGHBORS_LIMIT + 1]; // players occupying the startup locations

	size_t i, j;

	int reorder = 0;

	struct tile reachable[REACHABLE_LIMIT];
	size_t reachable_count;

	memset(locations, -1, sizeof(locations));
	memset(players, -1, sizeof(players));

	for(i = 0; i < battle->pawns_count; ++i)
	{
		const struct troop *troop = battle->pawns[i].troop;
		unsigned owner = troop->owner;
		signed char startup;

		// Find the prefered startup position for the pawn.
		if ((troop->location == battle->region) || (troop->location == LOCATION_GARRISON))
		{
			if (!allies(game, owner, battle->region->owner))
			{
				battle->pawns[i].startup = NEIGHBOR_SELF;
				continue; // skip troops attacking from the garrison
			}
			startup = NEIGHBOR_SELF;
			goto found;
		}
		else for(j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			if (troop->location != battle->region->neighbors[j]) continue;
			startup = j;
			goto found;
		}
		assert(0);

found:
		// If possible, set startup position for the pawn.
		if (players[startup] < 0) players[startup] = owner;
		else if (players[startup] != owner)
		{
			reorder = 1;
			startup = -1;
		}
		battle->pawns[i].startup = startup;

		if (locations[owner] < 0)
		{
			locations[owner] = startup;
			players_count += 1;
		}
	}

	if (reorder)
	{
		size_t locations_index = 0;

		if (players_count > NEIGHBORS_LIMIT + 1)
		{
			// TODO handle pawns when no place is available
			LOG_ERROR("no place for all players on the battlefield");
		}

		// Make sure no two startup locations are designated to the same player.
		for(i = 0; i < NEIGHBORS_LIMIT; ++i)
			if ((players[i] >= 0) && (locations[players[i]] != i))
				players[i] = -1;

		// Make sure each player has a designated startup location.
		for(i = 0; i < PLAYERS_LIMIT; ++i)
			if (locations[i] < 0)
			{
				while (players[locations_index] >= 0)
					locations_index += 1;
				locations[i] = locations_index;
			}

		// Update the startup location of each pawn.
		for(i = 0; i < battle->pawns_count; ++i)
		{
			unsigned owner = battle->pawns[i].troop->owner;
			if (!allies(game, owner, battle->region->owner)) continue; // skip troops attacking from the garrison
			battle->pawns[i].startup = locations[owner];
		}
	}

	// Place the pawns on the battlefield.
	for(i = 0; i < battle->pawns_count; ++i)
	{
		reachable_count = formation_reachable_open(game, battle, battle->pawns + i, reachable);
		for(j = 0; j < reachable_count; ++j)
		{
			struct battlefield *field = battle_field(battle, reachable[j]);
			if (!field->pawn)
			{
				pawn_place(battle, battle->pawns + i, reachable[j]);
				break;
			}
		}

		// TODO handle the case when all the place in the location is already taken
	}

	battle->defender = game->players[battle->region->owner].alliance;
}

static void battlefield_init_assault(const struct game *restrict game, struct battle *restrict battle)
{
	const struct garrison_info *restrict garrison = garrison_info(battle->region);

	size_t players_count = 0;
	signed char players[PLAYERS_LIMIT];

	size_t i, j;

	// Place the garrison at the top part of the battlefield.
	// . gate
	// # wall
	// O tower // TODO implement tower
	// #     #
	// .     .
	// O     O
	// ##O.O##

#define OBSTACLE(x, y, p, b) do \
	{ \
		struct battlefield *restrict field = &battle->field[y][x]; \
		unsigned blockage = (b); \
		field->blockage = blockage; \
		field->blockage_location = (p); \
		field->owner = battle->region->garrison.owner; \
		field->strength = ((blockage == BLOCKAGE_WALL) ? garrison->wall.health : garrison->gate.health); \
		field->unit = ((blockage == BLOCKAGE_WALL) ? &garrison->wall : &garrison->gate); \
	} while (0)

	OBSTACLE(9, 0, POSITION_TOP | POSITION_BOTTOM, BLOCKAGE_WALL);
	OBSTACLE(9, 1, POSITION_TOP | POSITION_BOTTOM, BLOCKAGE_GATE);
	OBSTACLE(9, 2, POSITION_TOP | POSITION_BOTTOM, BLOCKAGE_WALL);
	OBSTACLE(9, 3, POSITION_TOP | POSITION_RIGHT, BLOCKAGE_WALL);
	OBSTACLE(10, 3, POSITION_LEFT | POSITION_RIGHT, BLOCKAGE_WALL);
	OBSTACLE(11, 3, POSITION_LEFT | POSITION_RIGHT, BLOCKAGE_WALL);
	OBSTACLE(12, 3, POSITION_LEFT | POSITION_RIGHT, BLOCKAGE_GATE);
	OBSTACLE(13, 3, POSITION_LEFT | POSITION_RIGHT, BLOCKAGE_WALL);
	OBSTACLE(14, 3, POSITION_LEFT | POSITION_RIGHT, BLOCKAGE_WALL);
	OBSTACLE(15, 3, POSITION_TOP | POSITION_LEFT, BLOCKAGE_WALL);
	OBSTACLE(15, 2, POSITION_TOP | POSITION_BOTTOM, BLOCKAGE_WALL);
	OBSTACLE(15, 1, POSITION_TOP | POSITION_BOTTOM, BLOCKAGE_GATE);
	OBSTACLE(15, 0, POSITION_TOP | POSITION_BOTTOM, BLOCKAGE_WALL);

#undef OBSTACLE

	memset(players, -1, sizeof(players));

	// Place the pawns on the battlefield.
	for(i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *pawn = battle->pawns + i;

		struct tile reachable[REACHABLE_LIMIT];
		size_t reachable_count;

		if (pawn->startup != NEIGHBOR_GARRISON)
		{
			unsigned char owner = pawn->troop->owner;

			if (players[owner] < 0)
			{
				players[owner] = players_count;
				players_count += 1;
			}

			pawn->startup = players[owner];
			if (pawn->startup >= ASSAULT_LIMIT) continue; // TODO fix this
		}

		reachable_count = formation_reachable_assault(game, battle, pawn, reachable);
		for(j = 0; j < reachable_count; ++j)
		{
			struct battlefield *field = battle_field(battle, reachable[j]);
			if (!field->pawn)
			{
				pawn_place(battle, pawn, reachable[j]);
				break;
			}
		}

		// TODO handle the case when all the place in the location is already taken
	}

	battle->defender = game->players[battle->region->garrison.owner].alliance;
}

int battlefield_init(const struct game *restrict game, struct battle *restrict battle, struct region *restrict region, enum battle_type battle_type)
{
	unsigned troops_speed_count[1 + UNIT_SPEED_LIMIT] = {0};
	size_t troops_count = 0;

	struct troop *troop;

	struct pawn *pawns;
	size_t pawn_offset[PLAYERS_LIMIT] = {0};

	size_t i;

	int assault = (battle_type == BATTLE_ASSAULT);

	// Initialize each battle field as empty.
	size_t x, y;
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
	{
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			struct battlefield *field = &battle->field[y][x];
			field->tile = (struct tile){x, y};
			field->blockage = BLOCKAGE_NONE;
			field->blockage_location = 0;
			field->pawn = 0;
		}
	}

	// Count the troops participating in the battle and only those satisfying certain conditions.
	for(i = 0; i < PLAYERS_LIMIT; ++i) battle->players[i].pawns_count = 0;
	for(troop = region->troops; troop; troop = troop->_next)
	{
		if ((battle_type == BATTLE_OPEN) && (troop->location == LOCATION_GARRISON)) continue; // garrison troops don't participate in open battle
		if (assault && (troop->move != LOCATION_GARRISON)) continue; // only troops that specified it participate in assault

		// TODO temporary workaround
		if (battle->players[troop->owner].pawns_count == PAWNS_LIMIT)
			continue;

		troops_count += 1;

		// Count the troops owned by the given player.
		battle->players[troop->owner].pawns_count += 1;

		// Count the troops with the given speed.
		troops_speed_count[troop->unit->speed] += 1;
	}

	// Allocate memory for the pawns array and the player-specific pawns arrays.
	pawns = malloc(troops_count * sizeof(*pawns));
	if (!pawns) return ERROR_MEMORY;
	for(i = 0; i < PLAYERS_LIMIT; ++i)
	{
		if (!battle->players[i].pawns_count)
		{
			battle->players[i].pawns = 0;
			battle->players[i].state = PLAYER_DEAD;
			continue;
		}

		battle->players[i].pawns = malloc(battle->players[i].pawns_count * sizeof(*battle->players[i].pawns));
		if (!battle->players[i].pawns)
		{
			while (i--) free(battle->players[i].pawns);
			free(pawns);
			return ERROR_MEMORY;
		}
		battle->players[i].state = PLAYER_ALIVE;
	}

	// Sort the pawns by speed descending, using bucket sort.
	// Use troop counts to initialize offsets in the pawns array.
	unsigned offset[1 + UNIT_SPEED_LIMIT];
	offset[UNIT_SPEED_LIMIT] = 0;
	for(i = UNIT_SPEED_LIMIT; i--; )
		offset[i] = offset[i + 1] + troops_speed_count[i + 1];

	// Initialize pawn for each troop.
	for(troop = region->troops; troop; troop = troop->_next)
	{
		if ((battle_type == BATTLE_OPEN) && (troop->location == LOCATION_GARRISON)) continue; // garrison troops don't participate in open battle
		if (assault && (troop->move != LOCATION_GARRISON)) continue; // only troops that specified it participate in assault

		// TODO temporary workaround
		if (pawn_offset[troop->owner] == PAWNS_LIMIT)
			continue;

		i = offset[troop->unit->speed]++;

		pawns[i].troop = troop;
		pawns[i].count = troop->count;
		pawns[i].hurt = 0;
		pawns[i].attackers = 0;

		// pawns[i].position will be initialized during formation

		pawns[i].path.count = 0;
		pawns[i].moves = (struct array_moves){0};

		pawns[i].action = 0;

		if (troop->location == LOCATION_GARRISON) pawns[i].startup = NEIGHBOR_GARRISON;
		else pawns[i].startup = 0; // this will be initialized later

		battle->players[troop->owner].pawns[pawn_offset[troop->owner]++] = pawns + i;
	}

	battle->region = region;
	battle->assault = assault;
	battle->pawns = pawns;
	battle->pawns_count = troops_count;

	if (assault) battlefield_init_assault(game, battle);
	else battlefield_init_open(game, battle);

	return 0;
}

void battle_retreat(struct battle *restrict battle, unsigned char player)
{
	for(size_t i = 0; i < battle->players[player].pawns_count; ++i)
	{
		battle->players[player].pawns[i]->action = ACTION_HOLD;
		battle->players[player].pawns[i]->path.count = 0;
	}
	battle->players[player].state = PLAYER_RETREAT;
}

static void retreat(struct battle *restrict battle, unsigned char player)
{
	const struct garrison_info *restrict info;
	struct troop *restrict troop;
	unsigned garrison_places;

	// TODO currently attacker troops retreating from assault are killed; is this okay?
	// TODO currently open battle garrison reinforcements are killed; is this okay?

	info = garrison_info(battle->region);
	if (info && (battle->region->garrison.owner == player))
	{
		// Count the free places for troops in the garrison.
		garrison_places = info->troops;
		for(troop = battle->region->troops; troop; troop = troop->_next)
			if (troop->location == LOCATION_GARRISON)
				garrison_places -= 1;
	}
	else garrison_places = 0;

	for(size_t i = 0; i < battle->players[player].pawns_count; ++i)
	{
		troop = battle->players[player].pawns[i]->troop;

		if (troop->location == battle->region)
		{
			// If there is place for the troop in the garrison, move it there. Otherwise, kill it.
			if (garrison_places)
			{
				troop->move = troop->location = LOCATION_GARRISON;
				garrison_places -= 1;
			}
			else battle->players[player].pawns[i]->count = 0;
		}
		else if (troop->location == LOCATION_GARRISON)
		{
			// Kill troops that retreat from defending a garrison.
			battle->players[player].pawns[i]->count = 0;
		}
		else troop->move = troop->location;
	}

	battle->players[player].state = PLAYER_DEAD;
}

// Returns winner alliance number if the battle ended and -1 otherwise.
int battle_end(const struct game *restrict game, struct battle *restrict battle)
{
	int end = 1;
	signed char winner = -1;

	for(size_t i = 0; i < game->players_count; ++i)
	{
		unsigned char alliance;

		if (battle->players[i].state == PLAYER_RETREAT)
			retreat(battle, i);
		if (!battle->players[i].state)
			continue;

		alliance = game->players[i].alliance;

		battle->players[i].state = PLAYER_DEAD;
		for(size_t j = 0; j < battle->players[i].pawns_count; ++j)
		{
			if (battle->players[i].pawns[j]->count)
			{
				battle->players[i].state = PLAYER_ALIVE;

				if (winner < 0) winner = alliance;
				else if (alliance != winner) end = 0;
			}
		}
	}

	if (end) return ((winner >= 0) ? winner : battle->defender);
	else return -1;
}

void battlefield_term(const struct game *restrict game, struct battle *restrict battle)
{
	size_t i;
	for(i = 0; i < battle->pawns_count; ++i)
	{
		battle->pawns[i].troop->count = battle->pawns[i].count;
		array_moves_term(&battle->pawns[i].moves);
	}
	for(i = 0; i < game->players_count; ++i)
		free(battle->players[i].pawns);
	free(battle->pawns);
}
