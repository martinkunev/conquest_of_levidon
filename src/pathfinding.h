#include <math.h>

#define WALL_THICKNESS 0.5
#define WALL_OFFSET ((1 - WALL_THICKNESS) / 2) /* walls are placed in the middle of the field */

#define BATTLEFIELD_WIDTH 25
#define BATTLEFIELD_HEIGHT 25

#define PAWN_RADIUS 0.5

struct game;
struct battle;
struct pawn;

struct obstacles
{
	size_t count;
	struct obstacle
	{
		float left, right, top, bottom;
	} obstacle[];
};

struct position
{
	float x, y;
};

struct adjacency_list;

// Calculates the euclidean distance between a and b.
static inline double battlefield_distance(struct position a, struct position b)
{
	double dx = b.x - a.x, dy = b.y - a.y;
	return sqrt(dx * dx + dy * dy);
}

static inline int pawns_collide(struct position a, struct position b)
{
	return battlefield_distance(a, b) < (PAWN_RADIUS * 2);
}

static inline int in_battlefield(double x, double y)
{
	return ((x - PAWN_RADIUS >= 0) && (x + PAWN_RADIUS <= BATTLEFIELD_WIDTH) && (y - PAWN_RADIUS >= 0) && (y + PAWN_RADIUS <= BATTLEFIELD_HEIGHT));
}

struct obstacles *path_obstacles_alloc(const struct game *restrict game, const struct battle *restrict battle, unsigned char player);

struct adjacency_list *visibility_graph_build(const struct battle *restrict battle, const struct obstacles *restrict obstacles, unsigned vertices_reserved);
void visibility_graph_free(struct adjacency_list *graph);

int move_blocked_pawn(struct position start, struct position end, struct position pawn, double radius);

int path_visible(struct position origin, struct position target, const struct obstacles *restrict obstacles);
int path_find(struct pawn *restrict pawn, struct position destination, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles);
double path_distance(struct pawn *restrict pawn, struct position destination, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles);
int path_distances(const struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, double reachable[static BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH]);

unsigned path_moves_tangent(const struct pawn *restrict pawn, const struct pawn *restrict obstacle, double distance_covered, struct position moves[static restrict 2]);
