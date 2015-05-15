#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "movement.h"

#define heap_type struct troop *
#define heap_diff(a, b) ((a)->unit->speed >= (b)->unit->speed)
#include "heap.t"

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
	struct troop **troops, *slot;
	struct pawn *pawns;
	size_t count;

	size_t i, j;
	size_t x, y;

	// Initialize each field as empty.
	for(i = 0; i < BATTLEFIELD_HEIGHT; ++i)
	{
		for(j = 0; j < BATTLEFIELD_WIDTH; ++j)
		{
			battle->field[i][j].location = (struct point){j, i};
			battle->field[i][j].obstacle = OBSTACLE_NONE;
			battle->field[i][j].pawn = 0;
		}
	}

	count = 0;
	for(slot = region->troops; slot; slot = slot->_next)
		count += 1;
	pawns = malloc(count * sizeof(*pawns));
	if (!pawns) return -1;

	// Sort the troops by speed descending.
	troops = malloc(count * sizeof(*troops));
	if (!troops)
	{
		free(pawns);
		return -1;
	}
	struct heap heap = {.data = troops, .count = count};
	i = 0;
	for(slot = region->troops; slot; slot = slot->_next) troops[i++] = slot;
	heapify(&heap);
	while (--i)
	{
		slot = heap.data[0];
		heap_pop(&heap);
		troops[i] = slot;
	}

	memset(battle->player_pawns, 0, sizeof(battle->player_pawns));

	struct point reachable[REACHABLE_LIMIT];
	size_t reachable_count;

	// Initialize a pawn for each troop.
	for(i = 0; i < count; ++i)
	{
		pawns[i].slot = troops[i];
		pawns[i].hurt = 0;

		pawns[i].fight = POINT_NONE;
		pawns[i].shoot = POINT_NONE;

		if (vector_add(battle->player_pawns + troops[i]->player, pawns + i) < 0)
		{
			free(troops);
			free(pawns);
			for(i = 0; i < game->players_count; ++i)
				free(battle->player_pawns[i].data);
			return -1;
		}

		// Put the pawn at its initial position.
		reachable_count = formation_reachable(game, region, troops[i], reachable);
		for(j = 0; j < reachable_count; ++j)
			if (!battle->field[reachable[j].y][reachable[j].x].pawn)
				break;
		// assert(j < reachable_count);
		battle->field[reachable[j].y][reachable[j].x].pawn = pawns + i;

		pawns[i].moves = malloc(32 * sizeof(*pawns[i].moves)); // TODO fix this
		pawns[i].moves[0].location = reachable[j];
		pawns[i].moves[0].time = 0.0;
		pawns[i].moves_count = 1;
	}

	free(troops);

	battle->pawns = pawns;
	battle->pawns_count = count;
	return 0;
}

void battlefield_term(const struct game *restrict game, struct battle *restrict battle)
{
	size_t i;
	for(i = 0; i < game->players_count; ++i)
		free(battle->player_pawns[i].data);
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
		if (!battle->player_pawns[i].length) continue; // skip dead players

		alliance = game->players[i].alliance;

		alive = 0;
		for(j = 0; j < battle->player_pawns[i].length; ++j)
		{
			pawn = battle->player_pawns[i].data[j];
			if (pawn->slot->count)
			{
				alive = 1;

				if (winner < 0) winner = alliance;
				else if (alliance != winner) end = 0;
			}
		}

		// Mark players with no pawns left as dead.
		if (!alive) battle->player_pawns[i].length = 0;
	}

	if (end) return ((winner >= 0) ? winner : defender);
	else return -1;
}
