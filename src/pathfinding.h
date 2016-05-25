struct game;
struct battle;
struct pawn;

struct obstacles
{
	size_t count;
	struct obstacle
	{
		unsigned left, right, top, bottom;
	} obstacle[];
};

struct position
{
	float x, y;
};

struct adjacency_list
{
	size_t count;
	struct adjacency
	{
		struct position position;
		struct neighbor
		{
			size_t index;
			double distance;
			struct neighbor *next;
		} *neighbors;
	} list[];
};

// Calculates the euclidean distance between a and b.
static inline double battlefield_distance(struct position a, struct position b)
{
	int dx = b.x - a.x, dy = b.y - a.y;
	return sqrt(dx * dx + dy * dy);
}

struct obstacles *path_obstacles_alloc(const struct game *restrict game, const struct battle *restrict battle, unsigned char player);

int path_visible(struct position origin, struct position target, const struct obstacles *restrict obstacles);

struct adjacency_list *visibility_graph_build(const struct battle *restrict battle, const struct obstacles *restrict obstacles, unsigned vertices_reserved);
void visibility_graph_free(struct adjacency_list *graph);

// TODO think how to implement this
//int path_distances(const struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH]);

// Calculates the distance between origin and target and stores it in distance. On error, returns negative error code.
int path_distance(struct position origin, struct position target, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, double *restrict distance);

// Finds path from the pawn's current final location to a target field. Appends the path to the pawn's movement queue.
int path_find(struct pawn *restrict pawn, struct position target, struct adjacency_list *restrict nodes, const struct obstacles *restrict obstacles);
