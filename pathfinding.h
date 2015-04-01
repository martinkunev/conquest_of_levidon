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

struct adjacency_list *visibility_graph_build(const struct polygon *restrict obstacles, size_t obstacles_count);
void visibility_graph_free(struct adjacency_list *nodes);

struct pawn;
int path_find(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict nodes, const struct polygon *restrict obstacles, size_t obstacles_count);
