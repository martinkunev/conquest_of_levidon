#define PAWNS_LIMIT 12

#define REACHABLE_LIMIT 625 /* TODO fix this */

#define OWNER_NONE 0 /* sentinel alliance value used for walls */ /* TODO fix this: there could actually be an alliance with number 0 */

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
	enum {BLOCKAGE_NONE, BLOCKAGE_TERRAIN, BLOCKAGE_OBSTACLE, BLOCKAGE_TOWER} blockage;
	unsigned char position;

	signed char owner; // used for BLOCKAGE_OBSTACLE and BLOCKAGE_TOWER
	unsigned strength; // used for BLOCKAGE_OBSTACLE and BLOCKAGE_TOWER
	struct pawn *pawn; // used for BLOCKAGE_NONE and BLOCKAGE_TOWER
	enum armor armor; // used for BLOCKAGE_OBSTACLE and BLOCKAGE_TOWER

	// TODO for BLOCKAGE_TOWER there should be specified a field for descending
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

	unsigned round;
};

size_t formation_reachable_open(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct point reachable[REACHABLE_LIMIT]);
size_t formation_reachable_assault(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct point reachable[REACHABLE_LIMIT]);

int battlefield_neighbors(struct point a, struct point b);

int battlefield_init(const struct game *restrict game, struct battle *restrict battle, struct region *restrict region, int assault);
void battlefield_term(const struct game *restrict game, struct battle *restrict battle);

int battle_end(const struct game *restrict game, struct battle *restrict battle, unsigned char defender);
