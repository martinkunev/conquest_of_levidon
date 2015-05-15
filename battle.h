#define PAWNS_LIMIT 12

#define REACHABLE_LIMIT 100

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
	enum {OBSTACLE_NONE, OBSTACLE_FIXED, OBSTACLE_GATE, OBSTACLE_WALL} obstacle;

	struct pawn *pawn;
};

struct battle
{
	struct battlefield field[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];
	struct pawn *pawns;
	size_t pawns_count;
	struct vector player_pawns[PLAYERS_LIMIT];
};

static inline int point_eq(struct point a, struct point b)
{
	return ((a.x == b.x) && (a.y == b.y));
}

size_t formation_reachable(const struct game *restrict game, const struct region *restrict region, const struct troop *restrict troop, struct point reachable[REACHABLE_LIMIT]);

int battlefield_init(const struct game *restrict game, struct battle *restrict battle, struct region *restrict region);
void battlefield_term(const struct game *restrict game, struct battle *restrict battle);

int battle_end(const struct game *restrict game, struct battle *restrict battle, unsigned char defender);
