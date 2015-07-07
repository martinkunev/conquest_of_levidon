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
#define NEIGHBOR_GARRISON NEIGHBORS_LIMIT

#define ASSAULT_LIMIT 5

static inline double distance(const double *restrict origin, const double *restrict target)
{
	double dx = target[0] - origin[0], dy = target[1] - origin[1];
	return sqrt(dx * dx + dy * dy);
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
size_t formation_reachable_open(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct point reachable[REACHABLE_LIMIT])
{
	const double defend[2] = {12.5, 12.5};
	const double attack[NEIGHBORS_LIMIT][2] = {{25.0, 12.5}, {21.5, 3.5}, {12.5, 0.0}, {3.5, 3.5}, {0.0, 12.5}, {3.5, 21.5}, {12.5, 25.0}, {21.5, 21.5}};

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
					if (distance(defend, (double [2]){x + 0.5, y + 0.5}) <= FORMATION_RADIUS_DEFEND)
						reachable[reachable_count++] = (struct point){x, y};
				}
		}
		else // the pawn attacks from the garrison
		{
			for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
				for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
				{
					if (battle->field[y][x].blockage) continue;
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
				if (battle->field[y][x].blockage) continue;
				if (distance(defend, target) < FORMATION_RADIUS_FREE) continue;
				if (distance(attack[pawn->startup], target) <= FORMATION_RADIUS_ATTACK)
					reachable[reachable_count++] = (struct point){x, y};
			}
	}

	return reachable_count;
}

size_t formation_reachable_assault(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct point reachable[REACHABLE_LIMIT])
{
	const double defend[2] = {12.5, 0};
	const double assault[ASSAULT_LIMIT][2] = {{12.5, 25.0}, {0.0, 12.5}, {25.0, 12.5}, {3.5, 21.5}, {21.5, 21.5}};

	size_t x, y;

	size_t reachable_count = 0;

	if (pawn->startup == NEIGHBOR_GARRISON)
	{
		for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
			for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
			{
				if (battle->field[y][x].blockage) continue;
				if (distance(defend, (double [2]){x + 0.5, y + 0.5}) <= FORMATION_RADIUS_DEFEND)
					reachable[reachable_count++] = (struct point){x, y};
			}
	}
	else
	{
		for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
			for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
			{
				if (battle->field[y][x].blockage) continue;
				if (distance(assault[pawn->startup], (double [2]){x + 0.5, y + 0.5}) <= FORMATION_RADIUS_ATTACK)
					reachable[reachable_count++] = (struct point){x, y};
			}
	}

	return reachable_count;
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
			if (!field->pawn)
			{
				battle->pawns[i].moves = malloc(32 * sizeof(*battle->pawns[i].moves)); // TODO fix this
				battle->pawns[i].moves[0].location = reachable[j];
				battle->pawns[i].moves[0].time = 0.0;
				battle->pawns[i].moves_count = 1;

				field->pawn = battle->pawns + i;

				break;
			}
		}

		// TODO handle the case when all the place in the location is already taken
	}
}

static void battlefield_init_assault(const struct game *restrict game, struct battle *restrict battle)
{
	const struct garrison_info *restrict garrison = garrison_info(battle->region);

	size_t players_count = 0;
	signed char players[PLAYERS_LIMIT];

	size_t i, j;

	// Place the garrison at the top part of the battlefield.
	// #     #
	// .     .
	// #     #
	// ###.###

	// TODO: towers
	// #     #
	// .     .
	// O     O
	// ##O.O##

#define OBSTACLE(x, y, p, o, s, a) do \
	{ \
		struct battlefield *restrict field = &battle->field[y][x]; \
		field->blockage = BLOCKAGE_OBSTACLE; \
		field->position = (p); \
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
			if (!field->pawn)
			{
				pawn->moves = malloc(32 * sizeof(*pawn->moves)); // TODO fix this
				pawn->moves[0].location = reachable[j];
				pawn->moves[0].time = 0.0;
				pawn->moves_count = 1;

				field->pawn = pawn;

				break;
			}
		}

		// TODO handle the case when all the place in the location is already taken
	}
}

int battlefield_init(const struct game *restrict game, struct battle *restrict battle, struct region *restrict region, int assault)
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
			battle->field[y][x].blockage = BLOCKAGE_NONE;
			battle->field[y][x].pawn = 0;
		}
	}

	// Initialize count for the pawns array and for the player-specific pawns arrays.
	pawns_count = 0;
	for(i = 0; i < PLAYERS_LIMIT; ++i) battle->players[i].pawns_count = 0;
	for(troop = region->troops; troop; troop = troop->_next)
	{
		if (assault && (troop->move != troop->location)) continue; // troops coming to the region don't participate in the assault

		battle->players[troop->owner].pawns_count += 1;
		pawns_count += 1;
	}
	if (assault)
	{
		for(troop = region->garrison.troops; troop; troop = troop->_next)
		{
			battle->players[troop->owner].pawns_count += 1;
			pawns_count += 1;
		}
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
	if (assault)
		for(troop = region->garrison.troops; troop; troop = troop->_next)
			count[troop->unit->speed] += 1;
	offset[UNIT_SPEED_LIMIT] = 0;
	i = UNIT_SPEED_LIMIT;
	while (i--) offset[i] = offset[i + 1] + count[i + 1];

	// Initialize a pawn for each troop.
	for(troop = region->troops; troop; troop = troop->_next)
	{
		if (assault && (troop->move != troop->location)) continue; // troops coming to the region don't participate in the assault

		i = offset[troop->unit->speed]++;

		pawns[i].troop = troop;
		pawns[i].hurt = 0;
		pawns[i].action = 0;

		pawns[i].startup = 0; // this will be initialized later

		battle->players[troop->owner].pawns[pawn_offset[troop->owner]++] = pawns + i;
	}
	if (assault)
	{
		for(troop = region->garrison.troops; troop; troop = troop->_next)
		{
			i = offset[troop->unit->speed]++;

			pawns[i].troop = troop;
			pawns[i].hurt = 0;
			pawns[i].action = 0;

			pawns[i].startup = NEIGHBOR_GARRISON;

			battle->players[troop->owner].pawns[pawn_offset[troop->owner]++] = pawns + i;
		}
	}

	battle->region = region;
	battle->assault = assault;
	battle->pawns = pawns;
	battle->pawns_count = pawns_count;

	if (assault) battlefield_init_assault(game, battle);
	else battlefield_init_open(game, battle);

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
			if (pawn->troop->count)
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

int battlefield_neighbors(struct point a, struct point b)
{
	int distance;

	if (a.x == b.x) distance = (int)b.y - (int)a.y;
	else if (a.y == b.y) distance = (int)b.x - (int)a.x;
	else return 0;

	return ((distance == -1) || (distance == 1));
}
