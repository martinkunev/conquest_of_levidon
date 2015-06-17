#include <math.h>

#define BATTLEFIELD_WIDTH 25
#define BATTLEFIELD_HEIGHT 25

struct obstacle
{
	size_t vertices_count;
	enum {OBSTACLE_HORIZONTAL, OBSTACLE_VERTICAL} orientation;
	struct point points[];
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

struct pawn;

// Calculates the euclidean distance between a and b.
static inline double battlefield_distance(struct point a, struct point b)
{
	int dx = b.x - a.x, dy = b.y - a.y;
	return sqrt(dx * dx + dy * dy);
}

struct adjacency_list *visibility_graph_build(const struct obstacle *restrict obstacles, size_t obstacles_count);
void visibility_graph_free(struct adjacency_list *nodes);

int path_visible(struct point origin, struct point target, const struct obstacle *restrict obstacles, size_t obstacles_count);
int path_reachable(const struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacle *restrict obstacles, size_t obstacles_count, unsigned char reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH]);
int path_queue(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes, const struct obstacle *restrict obstacles, size_t obstacles_count);
