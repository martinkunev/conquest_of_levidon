#define BATTLEFIELD_WIDTH 32
#define BATTLEFIELD_HEIGHT 32

#include <math.h>

#include "types.h"

struct path_node
{
	struct point location;
	double distance;
	struct path_node *origin;
	size_t heap_index;
};

#define heap_type struct path_node *
#define heap_diff(a, b) ((a)->distance <= (b)->distance)
#define heap_update(heap, position) ((heap)->data[position]->heap_index = (position))
#include "heap.t"
#undef heap_update
#undef heap_diff
#undef heap_type

// Find the shortest path from an origin field to a target field.
// The path consists of a sequence of straight lines with each intermediate point being right angle of an obstacle.
// steps:
//  Build visibility graph with vertices the right angles of the obstacles, the origin point and the target point.
//  Use Dijkstra's algorithm to find the shortest path in the graph from origin to target.

// The fields are represented by their top left coordinates (in order to facilitate computations).
// The obstacles are represented as polygons surrounding the blocked area. The coordinates are exclusive (limit fields are passable).

// TODO create the necessary data structures: heap of pointers (reorder should update pointer values)

// TODO obstacles: rectangles, coastlines, fortresses
// TODO use bitmap to filter duplicated vertices
// TODO i can use the adjacency matrix to make the distances non-symmetric

static double field_distance(struct point a, struct point b)
{
	int dx = b.x - a.x, dy = b.y - a.y;
	return sqrt(dx * dx + dy * dy);
}

static int angle_positive(const struct polygon *restrict a, const struct polygon *restrict b, const struct polygon *restrict c)
{
	int fx = b.x - a.x, fy = b.y - a.y;
	int sx = c.x - b.x, sy = c.y - b.y;
	return ((fx * sy - sx * fy) > 0);
}

static inline long cross_product(struct point f, struct point s)
{
	return (f.x * s.y - s.x * f.y);
}

// Check if the wall (w0, w1) blocks the path (p0, p1)
static int blocks(struct point p0, struct point p1, struct point w0, struct point w1)
{
	// http://stackoverflow.com/questions/563198/how-do-you-detect-where-two-line-segments-intersect

	long cross = cross_product(p1, w1);

	if (cross) // the lines intersect
	{
		// the lines intersect where
		// p0 + (pm / cross) * p1 = w0 + (wm / cross) * w1
		struct point d = {w0.x - p0.x, w0.y - p0.y};
		wm = cross_product(d, p1);
		pm = cross_product(d, w1);

		// Simplify the terms in order to compare them easier.
		if (cross < 0)
		{
			cross = -cross;
			wm = -wm;
			pm = -pm;
		}

		// Check if the line segments intersect.
		if ((0 <= wm) && (wm < cross) && (0 <= pm) && (pm < cross))
			return 1;
	}

	return 0;
}

static int visible(struct point origin, struct point target, const struct polygon *restrict obstacles, size_t obstacles_count)
{
	const struct polygon *restrict obstacle;
	size_t i, j;

	// Check if there is a wall that blocks the path from origin to target.
	for(i = 0; i < obstacles_count; ++i)
	{
		obstacle = obstacles + i;

		if (blocks(origin, target, obstacle->points[obstacle->vertices_count - 1], obstacle->points[0]))
			return 0;
		for(j = 1; j < obstacle->vertices_count; ++j)
			if (blocks(origin, target, obstacle->points[j - 1], obstacle->points[j]))
				return 0;
	}

	return 1;
}

static void graph_attach(struct vector *restrict nodes, size_t index, double *restrict matrix, const struct polygon *restrict obstacles, size_t obstacles_count)
{
	size_t node;
	double distance;

	#define CONNECTED(i, j) matrix[i * nodes->length + j]

	CONNECTED(index, index) = INFINITY;
	for(node = 0; node < index; ++node)
	{
		if (visible(nodes->data + index, nodes->data + node, obstacles, obstacles_count))
		{
			distance = field_distance(nodes->data[index], nodes->data[node]);
			CONNECTED(index, node) = distance;
			CONNECTED(node, index) = distance;
		}
		else
		{
			CONNECTED(index, node) = INFINITY;
			CONNECTED(node, index) = INFINITY;
		}
	}

	#undef CONNECTED
}

// Stores the vertices of the graph in nodes and returns the adjacency matrix of the graph.
double *visibility_graph_build(const struct polygon *restrict obstacles, size_t obstacles_count, struct vector *restrict nodes)
{
	const struct polygon *restrict obstacle;
	size_t i, j;

	double *neighbors;

	*nodes = VECTOR_EMPTY;

	// Select the vertices forming positive angles for the visibility graph.
	for(i = 0; i < obstacles_count; ++i)
	{
		obstacle = obstacles + i;

		if (angle_positive(obstacle->points[obstacle->vertices_count - 2], obstacle->points[obstacle->vertices_count - 1], obstacle->points[0]))
			if (vector_add(nodes, obstacle->points + obstacle->vertices_count - 1))
				goto error;
		if (angle_positive(obstacle->points[obstacle->vertices_count - 1], obstacle->points[0], obstacle->points[1]))
			if (vector_add(nodes, obstacle->points + 0))
				goto error;
		for(j = 2; j < obstacle->vertices_count; ++j)
		{
			if (angle_positive(obstacle->points[j - 2], obstacle->points[j - 1], obstacle->points[j]))
				if (vector_add(nodes, obstacle->points + j - 1))
					goto error;
		}
	}

	// Add dummy vertices for the start and the end nodes.
	vector_add(nodes, 0);
	vector_add(nodes, 0);

	neighbors = malloc(nodes->length * nodes->length * sizeof(*neighbors));
	if (!neighbors) goto error;

	// Fill the adjacency matrix of the visibility graph.
	// Consider that no vertex is connected to itself.
	neighbors[0] = INFINITY;
	for(i = 1; i < nodes->length; ++i)
		graph_attach(nodes, i, nodes.data[i], neighbors, obstacles, obstacles_count);

	return neighbors;

error:
	vector_term(nodes);
	return 0;
}

void visibility_graph_free(struct vector *restrict nodes, double *matrix)
{
	vector_term(nodes);
	free(matrix);
}

void path_find(struct point origin, struct point target, struct vector *restrict nodes, double *restrict matrix, const struct polygon *restrict obstacles, size_t obstacles_count, struct vector *restrict moves)
{
	// Add target and origin points to the path graph.
	nodes->data[nodes->length - 2] = target;
	graph_attach(nodes, nodes->length - 2, matrix, obstacles, obstacles_count);
	nodes->data[nodes->length - 1] = origin;
	graph_attach(nodes, nodes->length - 1, matrix, obstacles, obstacles_count);

	size_t i;
	size_t last;

	struct path_node *traverse = 0;
	struct heap closest = {0};

	traverse = malloc(nodes->length * sizeof(*traverse));
	if (!traverse) goto error;

	closest.data = malloc((nodes->length - 1) * sizeof(traverse));
	if (!closest.data) goto error;

	// Build a heap from path nodes.
	for(i = 0; i < nodes->length - 1; ++i)
	{
		traverse[i].location = *(struct point *)nodes->data[i];
		traverse[i].distance = INFINITY;
		traverse[i].origin = 0;
		// heap_index will be set by heapify

		closest.data[i] = traverse + i;
	}
	//traverse[nodes->length - 1].distance = 0;
	closest.count = nodes->length;
	heapify(&closest);

	// Set origin point data.
	last = nodes->length - 1;
	traverse[last].location = origin;
	traverse[last].distance = 0;
	traverse[last].origin = 0;

	do
	{
		if (!closest.count) return; // TODO unreachable

		#define DISTANCE(i, j) matrix[i * nodes->length + j]

		// TODO should I instead heapify after all the distances are updated?
		for(i = 0; i < nodes->length; ++i)
		{
			if (DISTANCE(last, i) < INFINITY)
			{
				traverse[i].distance = traverse[last].distance + DISTANCE(last, i);
				traverse[i].origin = traverse + last;
				heap_emerge(&closest, traverse[i].heap_index);
			}
		}

		#undef CONNECTED

		last = heap_front(&closest) - traverse;
		if (traverse[last].distance == INFINITY) return; // TODO unreachable
		heap_pop_static(&closest);
	} while (traverse[last].location != target);

	free(closest.data);
	free(traverse);

	return; // TODO return result

error:
	free(closest.data);
	free(traverse);
	return; // TODO
}
