struct adjacency
{
	struct point location;
	struct neighbor
	{
		size_t index;
		double distance;
		struct neighbor *next;
	} *neighbors;
};

#define vector_type struct adjacency
#define vector_name vector_adjacency
#include "vector.t"
#undef vector_name
#undef vector_type

int visibility_graph_build(const struct polygon *restrict obstacles, size_t obstacles_count, struct vector_adjacency *restrict nodes);
void visibility_graph_free(struct vector_adjacency *nodes);

int path_find(struct point origin, struct point target, struct vector_adjacency *restrict nodes, const struct polygon *restrict obstacles, size_t obstacles_count, struct vector *restrict moves);
