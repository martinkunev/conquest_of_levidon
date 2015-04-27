#include <math.h>

struct pawn
{
	struct troop *slot;
	unsigned hurt;

	struct move *moves; // TODO allocate this
	size_t moves_size; // TODO rename this
	size_t moves_count;

	struct point fight, shoot;

	// used for movement computation
	struct point failback, step;
};

struct battlefield
{
	struct point location;
	enum {OBSTACLE_NONE, OBSTACLE_FIXED} obstacle;

	struct pawn *pawn;
};

struct battle
{
	struct battlefield field[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];
	struct pawn *pawns;
	size_t pawns_count;
	struct vector player_pawns[PLAYERS_LIMIT];
};

// Calculates the euclidean distance between a and b.
static inline double battlefield_distance(struct point a, struct point b)
{
	int dx = b.x - a.x, dy = b.y - a.y;
	return sqrt(dx * dx + dy * dy);
}

static inline int point_eq(struct point a, struct point b)
{
	return ((a.x == b.x) && (a.y == b.y));
}

#define PAWNS_LIMIT 12

/*
**ooooooo|oooo|ooooooo**

oooo*
ooo**
oo***
o***o
***oo
*/

static inline const struct point *formation_positions(const struct troop *restrict slot, const struct region *restrict region)
{
	static const struct point positions_defend[PAWNS_LIMIT] = {
		{12, 11}, {11, 11}, {11, 12}, {12, 12}, {13, 11}, {11, 10}, {10, 12}, {12, 13}, {13, 12}, {12, 10}, {10, 11}, {11, 13}
	};
	static const struct point positions_attack[NEIGHBORS_LIMIT][PAWNS_LIMIT] = {
		{{22, 11}, {22, 12}, {22, 10}, {22, 13}, {23, 11}, {23, 12}, {23, 10}, {23, 13}, {22, 9}, {22, 14}, {23, 9}, {23, 14}},
		{{20, 3}, {19, 2}, {21, 4}, {20, 2}, {21, 3}, {19, 1}, {22, 4}, {21, 2}, {20, 1}, {22, 3}, {19, 0}, {23, 4}},
		{{11, 1}, {12, 1}, {10, 1}, {13, 1}, {11, 0}, {12, 0}, {10, 0}, {13, 0}, {9, 1}, {14, 1}, {9, 0}, {14, 0}},
		{{3, 3}, {4, 2}, {2, 4}, {3, 2}, {2, 3}, {4, 1}, {1, 4}, {2, 2}, {3, 1}, {1, 3}, {4, 0}, {0, 4}},
		{{1, 11}, {1, 12}, {1, 10}, {1, 13}, {0, 11}, {0, 12}, {0, 10}, {0, 13}, {1, 9}, {1, 14}, {0, 9}, {0, 14}},
		{{3, 20}, {4, 21}, {2, 19}, {3, 21}, {2, 20}, {4, 22}, {1, 19}, {2, 21}, {3, 22}, {1, 20}, {4, 23}, {0, 19}},
		{{11, 22}, {12, 22}, {10, 22}, {13, 22}, {11, 23}, {12, 23}, {10, 23}, {13, 23}, {9, 22}, {14, 22}, {9, 23}, {14, 23}},
		{{20, 20}, {19, 21}, {21, 19}, {20, 21}, {21, 20}, {19, 22}, {22, 19}, {21, 21}, {20, 22}, {22, 20}, {19, 23}, {23, 19}},
	};

	size_t i;
	if (slot->location == region) return positions_defend;
	else for(i = 0; i < NEIGHBORS_LIMIT; ++i)
	{
		if (slot->location == region->neighbors[i])
			return positions_attack[i];
	}
}

int battlefield_init(const struct game *restrict game, struct battle *restrict battle, struct region *restrict region);
void battlefield_term(const struct game *restrict game, struct battle *restrict battle);

int battlefield_movement_plan(const struct player *restrict players, size_t players_count, struct battlefield battlefield[][BATTLEFIELD_HEIGHT], struct pawn *restrict pawns, size_t pawns_count);
void battlefield_movement_perform(struct battlefield battlefield[][BATTLEFIELD_HEIGHT], struct pawn *restrict pawns, size_t pawns_count);

int battle_end(const struct game *restrict game, struct battle *restrict battle, unsigned char defender);
