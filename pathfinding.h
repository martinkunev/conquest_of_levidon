#include <math.h>

#define BATTLEFIELD_WIDTH 24
#define BATTLEFIELD_HEIGHT 24

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

// Calculates the euclidean distance between a and b.
static inline double battlefield_distance(struct point a, struct point b)
{
	int dx = b.x - a.x, dy = b.y - a.y;
	return sqrt(dx * dx + dy * dy);
}

struct adjacency_list *visibility_graph_build(const struct polygon *restrict obstacles, size_t obstacles_count);
void visibility_graph_free(struct adjacency_list *nodes);

int path_visible(struct point origin, struct point target, const struct polygon *restrict obstacles, size_t obstacles_count);

struct pawn;
int path_reachable(const struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct polygon *restrict obstacles, size_t obstacles_count, unsigned char reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH]);
int path_queue(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes, const struct polygon *restrict obstacles, size_t obstacles_count);
