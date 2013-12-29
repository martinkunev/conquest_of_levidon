#define APPROX_ERROR 1e-3

//#define DEBUG

struct player
{
	unsigned char alliance;
};

struct unit
{
	unsigned char health;
	unsigned char damage;
	unsigned char speed;
};

struct slot
{
	struct slot *_prev, *_next;
	const struct unit *unit;
	unsigned count;
	unsigned char player;
};

#define SLOT_COUNT_MAX 256

#define BATTLEFIELD_WIDTH 15
#define BATTLEFIELD_HEIGHT 15

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
};

int reachable(const struct player *restrict players, struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], const struct pawn *restrict pawn, unsigned char x, unsigned char y);

void battle_init(struct pawn *battlefield[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct pawn *restrict pawns, size_t pawns_count);
int battle(const struct player *restrict players, size_t players_count, struct pawn *restrict pawns, size_t pawns_count);
