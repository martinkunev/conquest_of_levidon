#define APPROX_ERROR 1e-3

//#define DEBUG

#define SLOT_COUNT_MAX 256

#define BATTLEFIELD_WIDTH 24
#define BATTLEFIELD_HEIGHT 24

//#define BATTLEFIELD_WIDTH 32
//#define BATTLEFIELD_HEIGHT 32

#define ROUND_DURATION 8

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

	struct
	{
		int x, y;
	} shoot;
};

struct battle
{
	struct pawn *field[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];
	const struct player *restrict players;
	struct vector *player_pawns;
	size_t players_count;
};

int reachable(const struct player *restrict players, struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], const struct pawn *restrict pawn, unsigned char x, unsigned char y);
int shootable(const struct player *restrict players, struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], const struct pawn *restrict pawn, unsigned char x, unsigned char y);

int battle(const struct game *restrict game, struct region *restrict region);
