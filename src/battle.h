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

#define BATTLEFIELD_PAWNS_LIMIT 7

#define REACHABLE_LIMIT (BATTLEFIELD_WIDTH * BATTLEFIELD_HEIGHT)

#define OWNER_NONE 0 /* sentinel alliance value used for walls */ /* TODO fix this: neutral players should not own walls */

#define ASSAULT_LIMIT 5

#define PATH_QUEUE_LIMIT 8

#define NEIGHBOR_SELF NEIGHBORS_LIMIT
#define NEIGHBOR_GARRISON NEIGHBORS_LIMIT

enum battle_type {BATTLE_NONE, BATTLE_ASSAULT, BATTLE_OPEN, BATTLE_OPEN_REINFORCED};

struct battlefield;

struct pawn
{
	struct troop *troop;
	unsigned count, hurt, attackers;

	struct position position; // current pawn position
	struct position position_next; // expected pawn position at the next step

	struct array_moves moves; // pathfinding-generated movement to the next path position

	unsigned startup; // index of startup location on the battlefield or NEIGHBOR_SELF/NEIGHBOR_GARRISON

	// WARNING: Player-specific input variables below.

	struct
	{
		size_t count;
		struct position data[PATH_QUEUE_LIMIT];
	} path; // user-specified path of not yet reached positions

	enum pawn_action {ACTION_HOLD, ACTION_GUARD, ACTION_FIGHT, ACTION_SHOOT, ACTION_ASSAULT} action; // TODO maybe rename ACTION_FIGHT to ACTION_FOLLOW
	union
	{
		struct pawn *pawn; // for ACTION_FIGHT
		struct position position; // for ACTION_GUARD and ACTION_SHOOT
		struct battlefield *field; // for ACTION_ASSAULT
	} target;
};

enum {POSITION_RIGHT = 0x1, POSITION_TOP = 0x2, POSITION_LEFT = 0x4, POSITION_BOTTOM = 0x8};
struct battlefield
{
	struct tile tile;
	enum {BLOCKAGE_NONE, BLOCKAGE_TERRAIN, BLOCKAGE_WALL, BLOCKAGE_GATE} blockage;
	//enum {BLOCKAGE_NONE, BLOCKAGE_TERRAIN, BLOCKAGE_WALL, BLOCKAGE_GATE, BLOCKAGE_TOWER} blockage;
	unsigned char blockage_location;

	signed char owner; // for BLOCKAGE_GATE and BLOCKAGE_TOWER
	unsigned strength; // for BLOCKAGE_WALL, BLOCKAGE_GATE and BLOCKAGE_TOWER // TODO rename to health or store hurt instead (like for pawns)
	const struct unit *unit; // for BLOCKAGE_WALL, BLOCKAGE_GATE and BLOCKAGE_TOWER
	struct pawn *pawn; // during formation and for BLOCKAGE_TOWER TODO don't use this for formation?

	// TODO for BLOCKAGE_TOWER there should be specified a field for descending

	struct pawn *pawns[BATTLEFIELD_PAWNS_LIMIT]; // used only during player input; initialized by battlefield_index_build()
};

struct battle
{
	struct region *region;
	int assault;
	unsigned char defender;

	struct battlefield field[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH]; // information about positions with the respective coordinates when truncated
	size_t pawns_count;
	struct pawn *pawns;

	struct
	{
		size_t pawns_count;
		struct pawn **pawns;
		enum {PLAYER_DEAD, PLAYER_ALIVE, PLAYER_RETREAT} state;
	} players[PLAYERS_LIMIT];

	unsigned round;
};

extern const double formation_position_defend[2];
extern const double formation_position_attack[NEIGHBORS_LIMIT][2];
extern const double formation_position_garrison[2];
extern const double formation_position_assault[ASSAULT_LIMIT][2];

static inline struct battlefield *battle_field(struct battle *restrict battle, struct tile tile)
{
	return &battle->field[tile.y][tile.x];
}

size_t formation_reachable_open(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct tile reachable[REACHABLE_LIMIT]);
size_t formation_reachable_assault(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct tile reachable[REACHABLE_LIMIT]);

// Returns whether a pawn owned by the given player can pass through the field.
int battlefield_passable(const struct battlefield *restrict field, unsigned player);

int battlefield_init(const struct game *restrict game, struct battle *restrict battle, struct region *restrict region, enum battle_type battle_type);
void battlefield_term(const struct game *restrict game, struct battle *restrict battle);

void battle_retreat(struct battle *restrict battle, unsigned char player);
int battle_end(const struct game *restrict game, struct battle *restrict battle);
