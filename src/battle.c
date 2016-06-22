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

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "log.h"
#include "game.h"
#include "pathfinding.h"
#include "movement.h"
#include "battle.h"
#include "map.h"

#define FORMATION_RADIUS_ATTACK 3
#define FORMATION_RADIUS_DEFEND 4
#define FORMATION_RADIUS_FREE 10

const double formation_position_defend[2] = {12.5, 12.5};
const double formation_position_attack[NEIGHBORS_LIMIT][2] = {{25.0, 12.5}, {21.5, 3.5}, {12.5, 0.0}, {3.5, 3.5}, {0.0, 12.5}, {3.5, 21.5}, {12.5, 25.0}, {21.5, 21.5}};
const double formation_position_garrison[2] = {12.5, 0};
const double formation_position_assault[ASSAULT_LIMIT][2] = {{12.5, 20.0}, {0.0, 7.5}, {25.0, 7.5}, {3.5, 16.5}, {21.5, 16.5}};

static inline double distance(const double *restrict origin, const double *restrict target)
{
	double dx = target[0] - origin[0], dy = target[1] - origin[1];
	return sqrt(dx * dx + dy * dy);
}

// Returns whether the troop can be placed at the given field.
size_t formation_reachable_open(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct point reachable[REACHABLE_LIMIT])
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
						reachable[reachable_count++] = (struct point){x, y};
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
						reachable[reachable_count++] = (struct point){x, y};
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
					reachable[reachable_count++] = (struct point){x, y};
			}
	}

	return reachable_count;
}

size_t formation_reachable_assault(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct point reachable[REACHABLE_LIMIT])
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
					reachable[reachable_count++] = (struct point){x, y};
			}
	}
	else
	{
		for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
			for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
			{
				if (battle->field[y][x].blockage) continue;
				if (distance(formation_position_assault[pawn->startup], (double [2]){x + 0.5, y + 0.5}) <= FORMATION_RADIUS_ATTACK)
					reachable[reachable_count++] = (struct point){x, y};
			}
	}

	return reachable_count;
}

int battlefield_neighbors(struct point a, struct point b)
{
	int distance;

	if (a.x == b.x) distance = (int)b.y - (int)a.y;
	else if (a.y == b.y) distance = (int)b.x - (int)a.x;
	else return 0;

	return ((distance == -1) || (distance == 1));
}

int battlefield_passable(const struct game *restrict game, const struct battlefield *restrict field, unsigned player)
{
	// TODO review this function; field->pawns[0] no longer exists

	if (!field->blockage)
		return 1;
	if (field->blockage == BLOCKAGE_OBSTACLE)
		if (allies(game, player, field->owner) || field->pawns[0])
			return 1;
	return 0;
}

static void battlefield_init_open(const struct game *restrict game, struct battle *restrict battle)
{
	unsigned players_count = 0;
	signed char locations[PLAYERS_LIMIT]; // startup locations of the players participating in the battle
	signed char players[NEIGHBORS_LIMIT + 1]; // players occupying the startup locations

	size_t i, j;

	int reorder = 0;

	struct point reachable[REACHABLE_LIMIT];
	size_t reachable_count;

	memset(locations, -1, sizeof(locations));
	memset(players, -1, sizeof(players));

	for(i = 0; i < battle->pawns_count; ++i)
	{
		const struct troop *troop = battle->pawns[i].troop;
		unsigned owner = troop->owner;
		signed char startup;

		// Find the prefered startup position for the pawn.
		if (troop->location == battle->region)
		{
			if (!allies(game, owner, battle->region->owner))
			{
				battle->pawns[i].startup = NEIGHBOR_SELF;
				continue; // skip troops attacking from the garrison
			}
			startup = NEIGHBOR_SELF;
		}
		else for(j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			if (troop->location != battle->region->neighbors[j]) continue;
			startup = j;
			break;
		}
		// else assert(0);

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
			struct battlefield *field = &battle->field[reachable[j].y][reachable[j].x];
			if (!field->pawns[0])
			{
				pawn_place(battle, battle->pawns + i, reachable[j].x, reachable[j].y);
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

#define OBSTACLE(x, y, p, o, s, a) do \
	{ \
		struct battlefield *restrict field = &battle->field[y][x]; \
		field->blockage = BLOCKAGE_OBSTACLE; \
		field->location = (p); \
		field->owner = (o); \
		field->strength = (s); \
		field->armor = (a); \
	} while (0)

	OBSTACLE(9, 0, POSITION_TOP | POSITION_BOTTOM, OWNER_NONE, garrison->strength_wall, garrison->armor_wall);
	OBSTACLE(9, 1, POSITION_TOP | POSITION_BOTTOM, battle->region->garrison.owner, garrison->strength_gate, garrison->armor_gate);
	OBSTACLE(9, 2, POSITION_TOP | POSITION_BOTTOM, OWNER_NONE, garrison->strength_wall, garrison->armor_wall);
	OBSTACLE(9, 3, POSITION_TOP | POSITION_RIGHT, OWNER_NONE, garrison->strength_wall, garrison->armor_wall);
	OBSTACLE(10, 3, POSITION_LEFT | POSITION_RIGHT, OWNER_NONE, garrison->strength_wall, garrison->armor_wall);
	OBSTACLE(11, 3, POSITION_LEFT | POSITION_RIGHT, OWNER_NONE, garrison->strength_wall, garrison->armor_wall);
	OBSTACLE(12, 3, POSITION_LEFT | POSITION_RIGHT, battle->region->garrison.owner, garrison->strength_gate, garrison->armor_gate);
	OBSTACLE(13, 3, POSITION_LEFT | POSITION_RIGHT, OWNER_NONE, garrison->strength_wall, garrison->armor_wall);
	OBSTACLE(14, 3, POSITION_LEFT | POSITION_RIGHT, OWNER_NONE, garrison->strength_wall, garrison->armor_wall);
	OBSTACLE(15, 3, POSITION_TOP | POSITION_LEFT, OWNER_NONE, garrison->strength_wall, garrison->armor_wall);
	OBSTACLE(15, 2, POSITION_TOP | POSITION_BOTTOM, OWNER_NONE, garrison->strength_wall, garrison->armor_wall);
	OBSTACLE(15, 1, POSITION_TOP | POSITION_BOTTOM, battle->region->garrison.owner, garrison->strength_gate, garrison->armor_gate);
	OBSTACLE(15, 0, POSITION_TOP | POSITION_BOTTOM, OWNER_NONE, garrison->strength_wall, garrison->armor_wall);

#undef OBSTACLE

	memset(players, -1, sizeof(players));

	// Place the pawns on the battlefield.
	for(i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *pawn = battle->pawns + i;

		struct point reachable[REACHABLE_LIMIT];
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
			struct battlefield *field = &battle->field[reachable[j].y][reachable[j].x];
			if (!field->pawns[0])
			{
				pawn_place(battle, pawn, reachable[j].x, reachable[j].y);
				break;
			}
		}

		// TODO handle the case when all the place in the location is already taken
	}

	battle->defender = game->players[battle->region->garrison.owner].alliance;
}

int battlefield_init(const struct game *restrict game, struct battle *restrict battle, struct region *restrict region, int assault)
{
	unsigned troops_speed_count[1 + UNIT_SPEED_LIMIT] = {0};
	size_t troops_count = 0;

	struct troop *troop;

	struct pawn *pawns;
	size_t pawn_offset[PLAYERS_LIMIT] = {0};

	size_t i;

	// Initialize each battle field as empty.
	size_t x, y;
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
	{
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			struct battlefield *field = &battle->field[y][x];
			field->position = (struct position){x, y};
			field->blockage = BLOCKAGE_NONE;
			memset(field->pawns, 0, sizeof(field->pawns));
		}
	}

	// Count the troops participating in the battle and only those satisfying certain conditions.
	for(i = 0; i < PLAYERS_LIMIT; ++i) battle->players[i].pawns_count = 0;
	for(troop = region->troops; troop; troop = troop->_next)
	{
		if (!assault && (troop->location == LOCATION_GARRISON)) continue; // garrison troops don't participate in open battle
		if (assault && (troop->move != LOCATION_GARRISON)) continue; // only troops that specified it participate in assault

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
			battle->players[i].alive = 0;
			continue;
		}

		battle->players[i].pawns = malloc(battle->players[i].pawns_count * sizeof(*battle->players[i].pawns));
		if (!battle->players[i].pawns)
		{
			while (i--) free(battle->players[i].pawns);
			free(pawns);
			return ERROR_MEMORY;
		}
		battle->players[i].alive = 1;
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
		if (!assault && (troop->location == LOCATION_GARRISON)) continue; // garrison troops don't participate in open battle
		if (assault && (troop->move != LOCATION_GARRISON)) continue; // only troops that specified it participate in assault

		i = offset[troop->unit->speed]++;

		pawns[i].troop = troop;
		pawns[i].count = troop->count;
		pawns[i].dead = 0;
		pawns[i].hurt = 0;

		// pawns[i].position will be initialized during formation

		pawns[i].path = (struct array_moves){0};
		pawns[i].moves = (struct array_moves){0};

		pawns[i].action = ACTION_DEFAULT;

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

// Returns winner alliance number if the battle ended and -1 otherwise.
int battle_end(const struct game *restrict game, struct battle *restrict battle)
{
	int end = 1;

	signed char winner = -1;
	unsigned char alliance;

	struct pawn *pawn;

	size_t i, j;
	for(i = 0; i < game->players_count; ++i)
	{
		if (!battle->players[i].alive) continue;

		alliance = game->players[i].alliance;

		battle->players[i].alive = 0;
		for(j = 0; j < battle->players[i].pawns_count; ++j)
		{
			pawn = battle->players[i].pawns[j];
			if (pawn->count)
			{
				battle->players[i].alive = 1;

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
		array_moves_term(&battle->pawns[i].path);
		array_moves_term(&battle->pawns[i].moves);
	}
	for(i = 0; i < game->players_count; ++i)
		free(battle->players[i].pawns);
	free(battle->pawns);
}
