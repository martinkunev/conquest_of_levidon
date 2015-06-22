#define PAWNS_LIMIT 12

#define REACHABLE_LIMIT 625 /* TODO fix this */

struct pawn
{
	struct troop *troop;
	unsigned hurt;

	struct move *moves; // TODO allocate this
	size_t moves_size; // TODO rename this
	size_t moves_count;

	enum {PAWN_FIGHT = 1, PAWN_SHOOT, PAWN_ASSAULT} action;
	union
	{
		struct pawn *pawn;
		struct point field;
	} target;

	// used for movement computation
	struct point failback, step;

	unsigned startup;
};

enum {POSITION_RIGHT = 0x1, POSITION_TOP = 0x2, POSITION_LEFT = 0x4, POSITION_BOTTOM = 0x8};
struct battlefield
{
	struct point location;
	enum {BLOCKAGE_NONE, BLOCKAGE_TERRAIN, BLOCKAGE_OBSTACLE} blockage;
	unsigned char position;

	signed char owner; // used for BLOCKAGE_OBSTACLE
	unsigned strength; // used for BLOCKAGE_OBSTACLE
	struct pawn *pawn; // used for BLOCKAGE_NONE
};

struct battle
{
	const struct region *region;
	int assault;

	struct battlefield field[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];
	size_t pawns_count;
	struct pawn *pawns;

	struct
	{
		size_t pawns_count;
		struct pawn **pawns;
	} players[PLAYERS_LIMIT];
};

static inline int point_eq(struct point a, struct point b)
{
	return ((a.x == b.x) && (a.y == b.y));
}

size_t formation_reachable_open(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct point reachable[REACHABLE_LIMIT]);
size_t formation_reachable_assault(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct point reachable[REACHABLE_LIMIT]);

int battlefield_init(const struct game *restrict game, struct battle *restrict battle, struct region *restrict region, int assault);
void battlefield_term(const struct game *restrict game, struct battle *restrict battle);

int battle_end(const struct game *restrict game, struct battle *restrict battle, unsigned char defender);
