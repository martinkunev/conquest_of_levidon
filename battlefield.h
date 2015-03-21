#include <math.h>

#define BATTLEFIELD_WIDTH 24
#define BATTLEFIELD_HEIGHT 24

struct move
{
	struct point location;
	double distance;
	double time;
};

#define queue_type struct move
#include "queue.t"
#undef queue_type

struct pawn
{
	struct slot *slot;
	unsigned hurt;
	struct queue moves;
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

void moves_free(struct queue_item *item);

void pawn_stay(struct pawn *restrict pawn);

int battlefield_init(const struct game *restrict game, struct battle *restrict battle, struct region *restrict region);
void battlefield_term(const struct game *restrict game, struct battle *restrict battle);

int battlefield_reachable(struct battlefield battlefield[][BATTLEFIELD_WIDTH], struct pawn *restrict pawn, struct point target);
int battlefield_shootable(const struct pawn *restrict pawn, struct point target);

int battlefield_movement_plan(const struct player *restrict players, size_t players_count, struct battlefield battlefield[][BATTLEFIELD_HEIGHT], struct pawn *restrict pawns, size_t pawns_count);
void battlefield_movement_perform(struct battlefield battlefield[][BATTLEFIELD_HEIGHT], struct pawn *restrict pawns, size_t pawns_count);

void battlefield_fight(const struct game *restrict game, struct battle *restrict battle);
void battlefield_shoot(struct battle *battle);
void battlefield_clean_corpses(struct battle *battle);

int battle_end(const struct game *restrict game, struct battle *restrict battle, unsigned char defender);
