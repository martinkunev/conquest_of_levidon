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

struct move
{
	struct point location;
	double distance;
	double time;
};

#define queue_type struct move
#include "queue.t"
#undef queue_type

int visibility_graph_build(const struct polygon *restrict obstacles, size_t obstacles_count, struct adjacency_list *restrict nodes);
void visibility_graph_free(struct adjacency_list *nodes);

int path_find(struct queue *restrict moves, struct point target, struct adjacency_list *restrict nodes, const struct polygon *restrict obstacles, size_t obstacles_count);
