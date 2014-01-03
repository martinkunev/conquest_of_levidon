#define APPROX_ERROR 1e-3

//#define DEBUG

#include "map.h"

#define SLOT_COUNT_MAX 256

#define BATTLEFIELD_WIDTH 8
#define BATTLEFIELD_HEIGHT 8

struct pawn
{
	struct pawn *_prev, *_next;
	struct slot *slot;

	unsigned hurt;

	// Describes movement from (x[0], y[0]) to (x[1], y[1]). The movement starts at t[0] and ends at t[1].
	struct move
	{
		int x[2], y[2];
		double t[2];
	} move;

	// TODO ranged attack
};

struct battle
{
	struct pawn *field[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];
	const struct player *restrict players;
	struct vector *player_pawns;
	size_t players_count;
};

int reachable(const struct player *restrict players, struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], const struct pawn *restrict pawn, unsigned char x, unsigned char y);

int battle_init(struct battle *restrict battle, const struct player *restrict players, size_t players_count, struct pawn *restrict pawns, size_t pawns_count);
int battle(const struct player *restrict players, size_t players_count, struct pawn *pawns, size_t pawns_count);
