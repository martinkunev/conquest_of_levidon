#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "movement.h"

#define FORMATION_RADIUS_ATTACK 3
#define FORMATION_RADIUS_DEFEND 4
#define FORMATION_RADIUS_GARRISON 5
#define FORMATION_RADIUS_FREE 10

static inline double distance(const double *restrict origin, const double *restrict target)
{
	double dx = target[0] - origin[0], dy = target[1] - origin[1];
	return sqrt(dx * dx + dy * dy);
}

// Returns whether the troop can be placed at the given field.
size_t formation_reachable(const struct game *restrict game, const struct region *restrict region, const struct troop *restrict troop, struct point reachable[REACHABLE_LIMIT])
{
	const double defend[2] = {12.0, 12.0};
	const double attack[NEIGHBORS_LIMIT][2] = {{24.0, 12.0}, {20.5, 3.5}, {12.0, 0.0}, {3.5, 3.5}, {0.0, 12.0}, {3.5, 20.5}, {12.0, 24.0}, {20.5, 20.5}};

	size_t i;
	size_t x, y;

	size_t reachable_count = 0;

	if (troop->location == region)
	{
		if (game->players[region->owner].alliance == game->players[troop->player].alliance)
		{
			for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
				for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
					if (distance(defend, (double [2]){x + 0.5, y + 0.5}) <= FORMATION_RADIUS_DEFEND)
						reachable[reachable_count++] = (struct point){x, y};
		}
		else
		{
			for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
				for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
					if (distance(attack[region->garrison.position], (double [2]){x + 0.5, y + 0.5}) <= FORMATION_RADIUS_GARRISON)
						reachable[reachable_count++] = (struct point){x, y};
		}
	}
	else for(i = 0; i < NEIGHBORS_LIMIT; ++i)
	{
		if (troop->location == region->neighbors[i])
		{
			for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
				for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
				{
					double target[2] = {x + 0.5, y + 0.5};
					if (distance(defend, target) < FORMATION_RADIUS_FREE) continue;
					if (distance(attack[i], target) <= FORMATION_RADIUS_ATTACK)
						reachable[reachable_count++] = (struct point){x, y};
				}
		}
	}

	return reachable_count;
}

int battlefield_init(const struct game *restrict game, struct battle *restrict battle, struct region *restrict region)
{
	struct troop *troop;

	struct pawn *pawns;
	size_t pawns_count;
	size_t pawn_offset[PLAYERS_LIMIT] = {0};

	size_t i, j;
	size_t x, y;

	// Initialize each battle field as empty.
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
	{
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			battle->field[y][x].location = (struct point){x, y};
			battle->field[y][x].obstacle = OBSTACLE_NONE;
			battle->field[y][x].pawn = 0;
		}
	}

	// Initialize count for the pawns array and for the player-specific pawns arrays.
	pawns_count = 0;
	for(i = 0; i < PLAYERS_LIMIT; ++i) battle->players[troop->player].pawns_count = 0;
	for(troop = region->troops; troop; troop = troop->_next)
	{
		battle->players[troop->player].pawns_count += 1;
		pawns_count += 1;
	}

	// Allocate memory for the pawns array and the player-specific pawns arrays.
	pawns = malloc(pawns_count * sizeof(*pawns));
	if (!pawns) return -1;
	for(i = 0; i < PLAYERS_LIMIT; ++i)
	{
		if (!battle->players[i].pawns_count)
		{
			battle->players[i].pawns = 0;
			continue;
		}

		battle->players[i].pawns = malloc(battle->players[i].pawns_count * sizeof(*battle->players[i].pawns));
		if (!battle->players[i].pawns)
		{
			while (i--) free(battle->players[i].pawns);
			free(pawns);
			return -1;
		}
	}

	// Sort the pawns by speed descending using bucket sort.
	// Count the number of pawns for any given speed in the interval [0, UNIT_SPEED_LIMIT].
	// Use the counts to initialize offsets in the pawns array.
	unsigned count[1 + UNIT_SPEED_LIMIT] = {0}, offset[1 + UNIT_SPEED_LIMIT];
	for(troop = region->troops; troop; troop = troop->_next)
		count[troop->unit->speed] += 1;
	offset[UNIT_SPEED_LIMIT] = 0;
	i = UNIT_SPEED_LIMIT;
	while (i--) offset[i] = offset[i + 1] + count[i + 1];

	for(troop = region->troops; troop; troop = troop->_next)
	{
		i = offset[troop->unit->speed]++;

		pawns[i].slot = troop;
		pawns[i].hurt = 0;
		pawns[i].fight = POINT_NONE;
		pawns[i].shoot = POINT_NONE;

		pawns[i].moves[0].location = POINT_NONE; // TODO this is supposed to set the position of pawns for which there is no place on the battlefield; is this okay?

		battle->players[troop->player].pawns[pawn_offset[troop->player]++] = pawns + i;
	}

	struct point reachable[REACHABLE_LIMIT];
	size_t reachable_count;

	// Initialize a pawn for each troop.
	for(i = 0; i < pawns_count; ++i)
	{
		// Find where to place the pawn. Skip it if there is no place available.
		reachable_count = formation_reachable(game, region, pawns[i].slot, reachable);
		for(j = 0; j < reachable_count; ++j)
		{
			if (!battle->field[reachable[j].y][reachable[j].x].pawn)
			{
				pawns[i].moves = malloc(32 * sizeof(*pawns[i].moves)); // TODO fix this
				pawns[i].moves[0].location = reachable[j];
				pawns[i].moves[0].time = 0.0;
				pawns[i].moves_count = 1;

				battle->field[reachable[j].y][reachable[j].x].pawn = pawns + i;

				break;
			}
		}
	}

	battle->pawns = pawns;
	battle->pawns_count = pawns_count;
	return 0;
}

void battlefield_term(const struct game *restrict game, struct battle *restrict battle)
{
	size_t i;
	for(i = 0; i < game->players_count; ++i)
		free(battle->players[i].pawns);
	free(battle->pawns);
}

// Returns winner alliance number if the battle ended and -1 otherwise.
int battle_end(const struct game *restrict game, struct battle *restrict battle, unsigned char defender)
{
	int end = 1;
	int alive;

	signed char winner = -1;
	unsigned char alliance;

	struct pawn *pawn;

	size_t i, j;
	for(i = 0; i < game->players_count; ++i)
	{
		if (!battle->players[i].pawns_count) continue; // skip dead players

		alliance = game->players[i].alliance;

		alive = 0;
		for(j = 0; j < battle->players[i].pawns_count; ++j)
		{
			pawn = battle->players[i].pawns[j];
			if (pawn->slot->count)
			{
				alive = 1;

				if (winner < 0) winner = alliance;
				else if (alliance != winner) end = 0;
			}
		}

		// Mark players with no pawns left as dead.
		if (!alive) battle->players[i].pawns_count = 0;
	}

	if (end) return ((winner >= 0) ? winner : defender);
	else return -1;
}
