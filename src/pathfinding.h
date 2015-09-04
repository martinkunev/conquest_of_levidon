#include <math.h>

#define BATTLEFIELD_WIDTH 25
#define BATTLEFIELD_HEIGHT 25

struct obstacles
{
	size_t count;
	struct obstacle
	{
		struct point p[2];
	} obstacle[];
};

struct move
{
	struct point location;
	double time;
};

struct adjacency_list
{
	size_t count;
	struct adjacency
	{
		struct point location;
		struct neighbor
		{
			size_t index;
			double distance;
			struct neighbor *next;
		} *neighbors;
	} list[];
};

struct battle;
struct pawn;

// Calculates the euclidean distance between a and b.
static inline double battlefield_distance(struct point a, struct point b)
{
	int dx = b.x - a.x, dy = b.y - a.y;
	return sqrt(dx * dx + dy * dy);
}

struct obstacles *path_obstacles(const struct game *restrict game, const struct battle *restrict battle, unsigned char player);

struct adjacency_list *visibility_graph_build(const struct battle *restrict battle, const struct obstacles *restrict obstacles);
void visibility_graph_free(struct adjacency_list *graph);

int target_visible(struct point origin, struct point target, const struct obstacles *restrict obstacles);

int path_distances(const struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH]);
int path_distance(struct point origin, struct point target, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, double *restrict distance);
int path_queue(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes, const struct obstacles *restrict obstacles);
