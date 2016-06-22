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

#define BATTLEFIELD_WIDTH 25
#define BATTLEFIELD_HEIGHT 25

#define PAWNS_LIMIT 12

#define REACHABLE_LIMIT 625 /* TODO fix this */

#define OWNER_NONE 0 /* sentinel alliance value used for walls */ /* TODO fix this: neutral players should not own walls */

#define ASSAULT_LIMIT 5

#define NEIGHBOR_SELF NEIGHBORS_LIMIT
#define NEIGHBOR_GARRISON NEIGHBORS_LIMIT

struct point
{
	int x, y;
};

struct battlefield;

struct pawn
{
	struct troop *troop;
	unsigned count;
	unsigned dead; // TODO is this necessary?
	unsigned hurt;

	struct position position;
	struct position position_next;

	struct array_moves path; // user-specified path of not yet reached positions
	struct array_moves moves; // pathfinding-generated movement to the next path position

	enum {ACTION_DEFAULT, ACTION_FIGHT, ACTION_SHOOT, ACTION_ASSAULT} action;
	union
	{
		struct pawn *pawn; // for ACTION_FIGHT
		struct battlefield *field; // for ACTION_SHOOT and ACTION_ASSAULT
	} target;

	unsigned startup; // index of startup location on the battlefield
};

enum {POSITION_RIGHT = 0x1, POSITION_TOP = 0x2, POSITION_LEFT = 0x4, POSITION_BOTTOM = 0x8};
struct battlefield
{
	struct position position;
	//enum {BLOCKAGE_NONE, BLOCKAGE_TERRAIN, BLOCKAGE_OBSTACLE, BLOCKAGE_TOWER} blockage;
	enum {BLOCKAGE_NONE, BLOCKAGE_TERRAIN, BLOCKAGE_OBSTACLE} blockage;
	unsigned char location; // blockage location

	signed char owner; // for BLOCKAGE_OBSTACLE and BLOCKAGE_TOWER
	unsigned strength; // for BLOCKAGE_OBSTACLE and BLOCKAGE_TOWER
//	struct pawn *pawns[4]; // for BLOCKAGE_NONE and BLOCKAGE_TOWER
	struct pawn *pawn; // for BLOCKAGE_TOWER
	enum armor armor; // for BLOCKAGE_OBSTACLE and BLOCKAGE_TOWER

	// TODO for BLOCKAGE_TOWER there should be specified a field for descending
};

struct battle
{
	const struct region *region;
	int assault;
	unsigned char defender;

	struct battlefield field[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH]; // information about positions with the respective coordinates when truncated
	size_t pawns_count;
	struct pawn *pawns;

	struct
	{
		size_t pawns_count;
		struct pawn **pawns;
		int alive; // TODO this should be renamed to reflect its real use; it is currently used for aliveness and surrender
	} players[PLAYERS_LIMIT];

	unsigned round;
};

extern const double formation_position_defend[2];
extern const double formation_position_attack[NEIGHBORS_LIMIT][2];
extern const double formation_position_garrison[2];
extern const double formation_position_assault[ASSAULT_LIMIT][2];

size_t formation_reachable_open(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct point reachable[REACHABLE_LIMIT]);
size_t formation_reachable_assault(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct point reachable[REACHABLE_LIMIT]);

int battlefield_neighbors(struct point a, struct point b);

// Returns whether a pawn owned by the given player can pass through the field.
int battlefield_passable(const struct game *restrict game, const struct battlefield *restrict field, unsigned player);

int battlefield_init(const struct game *restrict game, struct battle *restrict battle, struct region *restrict region, int assault);
void battlefield_term(const struct game *restrict game, struct battle *restrict battle);

int battle_end(const struct game *restrict game, struct battle *restrict battle);
