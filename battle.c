#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "movement.h"

#define FORMATION_RADIUS_ATTACK 3
#define FORMATION_RADIUS_DEFEND 4
#define FORMATION_RADIUS_FREE 10

#define NEIGHBOR_SELF NEIGHBORS_LIMIT

static inline double distance(const double *restrict origin, const double *restrict target)
{
	double dx = target[0] - origin[0], dy = target[1] - origin[1];
	return sqrt(dx * dx + dy * dy);
}

static inline int allies(const struct game *game, unsigned player0, unsigned player1)
{
	return (game->players[player0].alliance == game->players[player1].alliance);
}

static struct polygon *region_create(size_t count, ...)
{
	size_t index;
	va_list vertices;

	// Allocate memory for the region and its vertices.
	struct polygon *polygon = malloc(offsetof(struct polygon, points) + count * sizeof(struct point));
	if (!polygon) return 0;
	polygon->vertices_count = count;

	// Initialize region vertices.
	va_start(vertices, count);
	for(index = 0; index < count; ++index)
		polygon->points[index] = va_arg(vertices, struct point);
	va_end(vertices);

	return polygon;
}

// Returns whether the troop can be placed at the given field.
size_t formation_reachable(const struct game *restrict game, const struct region *restrict region, const struct pawn *restrict pawn, struct point reachable[REACHABLE_LIMIT])
{
	const double defend[2] = {12.0, 12.0};
	const double attack[NEIGHBORS_LIMIT][2] = {{24.0, 12.0}, {20.5, 3.5}, {12.0, 0.0}, {3.5, 3.5}, {0.0, 12.0}, {3.5, 20.5}, {12.0, 24.0}, {20.5, 20.5}};

	size_t x, y;

	size_t reachable_count = 0;

	if (pawn->startup == NEIGHBOR_SELF)
	{
		if (allies(game, region->owner, pawn->slot->owner))
		{
			for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
				for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
					if (distance(defend, (double [2]){x + 0.5, y + 0.5}) <= FORMATION_RADIUS_DEFEND)
						reachable[reachable_count++] = (struct point){x, y};
		}
		else // the pawn attacks from the garrison
		{
			for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
				for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
				{
					if (distance(defend, (double [2]){x + 0.5, y + 0.5}) <= FORMATION_RADIUS_DEFEND) continue;
					if (distance(defend, (double [2]){x + 0.5, y + 0.5}) < FORMATION_RADIUS_FREE)
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
				if (distance(defend, target) < FORMATION_RADIUS_FREE) continue;
				if (distance(attack[pawn->startup], target) <= FORMATION_RADIUS_ATTACK)
					reachable[reachable_count++] = (struct point){x, y};
			}
	}

	return reachable_count;
}

static void battlefield_init_formation(const struct game *restrict game, struct battle *restrict battle)
{
	unsigned players_count = 0;
	signed char locations[PLAYERS_LIMIT]; // startup locations of the players participating in the battle
	signed char players[NEIGHBORS_LIMIT]; // players occupying the startup locations

	size_t i, j;

	int reorder = 0;

	struct point reachable[REACHABLE_LIMIT];
	size_t reachable_count;

	memset(locations, -1, sizeof(locations));
	memset(players, -1, sizeof(players));

	for(i = 0; i < battle->pawns_count; ++i)
	{
		const struct troop *troop = battle->pawns[i].slot;
		unsigned owner = troop->owner;
		signed char startup;

		// Find the prefered startup position for the pawn.
		if (troop->location == battle->region)
		{
			if (!allies(game, owner, battle->region->owner)) continue; // skip troops attacking from the garrison
			startup = NEIGHBOR_SELF;
		}
		else for(j = 0; j < NEIGHBORS_LIMIT; ++j)
		{
			if (troop->location != battle->region->neighbors[j]) continue;
			startup = j;
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
			; // TODO handle pawns when no place is available

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
			unsigned owner = battle->pawns[i].slot->owner;
			if (!allies(game, owner, battle->region->owner)) continue; // skip troops attacking from the garrison
			battle->pawns[i].startup = locations[owner];
		}
	}

	// Place the pawns on the battlefield.
	for(i = 0; i < battle->pawns_count; ++i)
	{
		reachable_count = formation_reachable(game, battle->region, battle->pawns + i, reachable);
		for(j = 0; j < reachable_count; ++j)
		{
			struct battlefield *field = &battle->field[reachable[j].y][reachable[j].x];
			if (!field->obstacle && !field->pawn)
			{
				battle->pawns[i].moves = malloc(32 * sizeof(*battle->pawns[i].moves)); // TODO fix this
				battle->pawns[i].moves[0].location = reachable[j];
				battle->pawns[i].moves[0].time = 0.0;
				battle->pawns[i].moves_count = 1;

				field->pawn = battle->pawns + i;

				break;
			}
		}
	}
}

int battlefield_init(const struct game *restrict game, struct battle *restrict battle, struct region *restrict region)
{
	struct troop *troop;

	struct pawn *pawns;
	size_t pawns_count;
	size_t pawn_offset[PLAYERS_LIMIT] = {0};

	size_t i;
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
	for(i = 0; i < PLAYERS_LIMIT; ++i) battle->players[i].pawns_count = 0;
	for(troop = region->troops; troop; troop = troop->_next)
	{
		battle->players[troop->owner].pawns_count += 1;
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

	// Initialize a pawn for each troop.
	for(troop = region->troops; troop; troop = troop->_next)
	{
		i = offset[troop->unit->speed]++;

		pawns[i].slot = troop;
		pawns[i].hurt = 0;
		pawns[i].fight = POINT_NONE;
		pawns[i].shoot = POINT_NONE;

		pawns[i].moves[0].location = POINT_NONE; // TODO this is supposed to set the position of pawns for which there is no place on the battlefield; is this okay?

		battle->players[troop->owner].pawns[pawn_offset[troop->owner]++] = pawns + i;
	}

	battle->region = region;
	battle->pawns = pawns;
	battle->pawns_count = pawns_count;

	battlefield_init_formation(game, battle);

	return 0;
}

/*static unsigned obstacles_count(const struct battle *battle, struct point p0, struct point p1)
{
	int x = p0.x, y = p0.y;
	int step;

	int in_wall = 0;

	unsigned count = 0;

	if (p0.x == p1.x) // vertical wall
	{
		step = ((p1.y >= p0.y) ? +1 : -1);
		while (1)
		{
			if (battle->field[y][x].strength) in_wall = 1;
			else if (in_wall)
			{
				in_wall = 0;
				count += 1;
			}

			if (y == p1.y) break;
			y += step;
		}
	}
	else // horizontal wall
	{
		// assert(p0.y == p1.y)

		step = ((p1.x >= p0.x) ? +1 : -1);
		while (1)
		{
			if (battle->field[y][x].strength) in_wall = 1;
			else if (in_wall)
			{
				in_wall = 0;
				count += 1;
			}

			if (x == p1.x) break;
			x += step;
		}
	}

	if (in_wall) count += 1;
	return count;
}

int assault_obstacles(const struct game *restrict game, struct region *restrict region, unsigned char player)
{
	if (allies(game, player, region->garrison.owner))
	{
		//obstacles = region_create(2, (struct point){0, 9}, (struct point){0, 9});
	}
	else
	{
		// TODO don't create the destroyed walls
		//
	}
}*/

void assault_init(const struct game *restrict game, struct battle *restrict battle, struct region *restrict region)
{
	const struct garrison_info *restrict garrison = garrison_info(region);

	// Place the garrison at the top part of the battlefield.
	// #     #
	// .     .
	// #     #
	// ###.###

	battle->field[0][9].obstacle = OBSTACLE_WALL;
	battle->field[0][9].strength = garrison->strength_wall;

	battle->field[1][9].obstacle = OBSTACLE_GATE;
	battle->field[1][9].strength = garrison->strength_gate;
	battle->field[1][9].owner = region->garrison.owner;

	battle->field[2][9].obstacle = OBSTACLE_WALL;
	battle->field[2][9].strength = garrison->strength_wall;

	battle->field[3][9].obstacle = OBSTACLE_WALL;
	battle->field[3][9].strength = garrison->strength_wall;

	battle->field[3][10].obstacle = OBSTACLE_WALL;
	battle->field[3][10].strength = garrison->strength_wall;

	battle->field[3][11].obstacle = OBSTACLE_WALL;
	battle->field[3][11].strength = garrison->strength_wall;

	battle->field[3][12].obstacle = OBSTACLE_GATE;
	battle->field[3][12].strength = garrison->strength_gate;
	battle->field[3][12].owner = region->garrison.owner;

	battle->field[3][13].obstacle = OBSTACLE_WALL;
	battle->field[3][13].strength = garrison->strength_wall;

	battle->field[3][14].obstacle = OBSTACLE_WALL;
	battle->field[3][14].strength = garrison->strength_wall;

	battle->field[3][15].obstacle = OBSTACLE_WALL;
	battle->field[3][15].strength = garrison->strength_wall;

	battle->field[2][15].obstacle = OBSTACLE_WALL;
	battle->field[2][15].strength = garrison->strength_wall;

	battle->field[1][15].obstacle = OBSTACLE_GATE;
	battle->field[1][15].strength = garrison->strength_gate;
	battle->field[1][15].owner = region->garrison.owner;

	battle->field[0][15].obstacle = OBSTACLE_WALL;
	battle->field[0][15].strength = garrison->strength_wall;
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
