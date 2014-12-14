#include <math.h>

#include "types.h"
#include "display.h"
#include "pathfinding.h"

struct path_node
{
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

// TODO obstacles: rectangles, coastlines, fortresses
// TODO use bitmap to filter duplicated vertices
// TODO do I need non-symmetric distance?

static double field_distance(struct point a, struct point b)
{
	int dx = b.x - a.x, dy = b.y - a.y;
	return sqrt(dx * dx + dy * dy);
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
		long wm, pm;
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

static int graph_attach(struct vector_adjacency *restrict nodes, size_t index, const struct polygon *restrict obstacles, size_t obstacles_count)
{
	size_t node;
	struct point from, to;
	struct neighbor *neighbor;
	double distance;

	for(node = 0; node < index; ++node)
	{
		from = nodes->data[index].location;
		to = nodes->data[node].location;
		if (visible(from, to, obstacles, obstacles_count))
		{
			distance = field_distance(from, to);

			neighbor = malloc(sizeof(*neighbor));
			if (!neighbor) return ERROR_MEMORY;
			neighbor->index = index;
			neighbor->distance = distance;
			neighbor->next = nodes->data[index].neighbors;
			nodes->data[index].neighbors = neighbor;

			neighbor = malloc(sizeof(*neighbor));
			if (!neighbor) return ERROR_MEMORY;
			neighbor->index = node;
			neighbor->distance = distance;
			neighbor->next = nodes->data[node].neighbors;
			nodes->data[node].neighbors = neighbor;
		}
	}

	return 0;
}

static inline int angle_positive(struct point a, struct point b, struct point c)
{
	int fx = b.x - a.x, fy = b.y - a.y;
	int sx = c.x - b.x, sy = c.y - b.y;
	return ((fx * sy - sx * fy) > 0);
}
static int graph_insert(struct vector_adjacency *nodes, struct point a, struct point b, struct point c)
{
	if (angle_positive(a, b, c))
	{
		struct adjacency *node = vector_adjacency_insert(nodes);
		if (!node) return ERROR_MEMORY;
		node->location = b;
		node->neighbors = 0;
	}
	return 0;
}

// Stores the vertices of the graph in nodes and returns the adjacency matrix of the graph.
int visibility_graph_build(const struct polygon *restrict obstacles, size_t obstacles_count, struct vector_adjacency *restrict nodes)
{
	const struct polygon *restrict obstacle;
	size_t i, j;

	nodes->data = 0;
	nodes->length = 0;
	nodes->size = 0;

	// Select the vertices forming positive angles for the visibility graph.
	for(i = 0; i < obstacles_count; ++i)
	{
		obstacle = obstacles + i;

		if (graph_insert(nodes, obstacle->points[obstacle->vertices_count - 2], obstacle->points[obstacle->vertices_count - 1], obstacle->points[0]) < 0)
			goto error;
		if (graph_insert(nodes, obstacle->points[obstacle->vertices_count - 1], obstacle->points[0], obstacle->points[1]) < 0)
			goto error;
		for(j = 2; j < obstacle->vertices_count; ++j)
			if (graph_insert(nodes, obstacle->points[j - 2], obstacle->points[j - 1], obstacle->points[j]) < 0)
				goto error;
	}

	// Add dummy vertices for the start and the end nodes.
	vector_adjacency_insert(nodes);
	vector_adjacency_insert(nodes);

	// Fill the adjacency list of the visibility graph.
	// Consider that no vertex is connected to itself.
	for(i = 1; i < nodes->length; ++i)
		if (graph_attach(nodes, i, obstacles, obstacles_count) < 0)
			goto error;

	return 0;

error:
	visibility_graph_free(nodes);
	return ERROR_MEMORY;
}

void visibility_graph_free(struct vector_adjacency *nodes)
{
	size_t i;
	struct neighbor *item, *next;

	for(i = 0; i < nodes->length; ++i)
	{
		item = nodes->data[i].neighbors;
		while (item)
		{
			next = item->next;
			free(item);
			item = next;
		}
	}
	free(nodes->data);
}

int path_find(struct point origin, struct point target, struct vector_adjacency *restrict nodes, const struct polygon *restrict obstacles, size_t obstacles_count, struct vector *restrict moves)
{
	const size_t node_origin = nodes->length - 1, node_target = nodes->length - 2;

	// Add target and origin points to the path graph.
	nodes->data[node_target].location = target;
	graph_attach(nodes, node_target, obstacles, obstacles_count);
	nodes->data[node_origin].location = origin;
	graph_attach(nodes, node_origin, obstacles, obstacles_count);

	size_t i;
	size_t last;

	struct path_node *traverse = 0, *from, *temp;
	struct heap closest = {0};

	struct neighbor *neighbor;
	double distance;

	traverse = malloc(nodes->length * sizeof(*traverse));
	if (!traverse) return ERROR_MEMORY;

	closest.data = malloc((nodes->length - 1) * sizeof(traverse));
	if (!closest.data)
	{
		free(traverse);
		return ERROR_MEMORY;
	}

	// Build a heap from path nodes.
	for(i = 0; i < nodes->length - 1; ++i)
	{
		traverse[i].distance = INFINITY;
		traverse[i].origin = 0;
		// heap_index will be set by heapify

		closest.data[i] = traverse + i;
	}
	closest.count = nodes->length;
	heapify(&closest);

	// Set origin point data.
	last = node_origin;
	traverse[last].distance = 0;
	traverse[last].origin = 0;

	// Find the shortest path to target using Dijkstra's algorithm.
	do
	{
		if (!closest.count) goto error;

		// Update path from last to its neighbors.
		neighbor = nodes->data[last].neighbors;
		while (neighbor)
		{
			distance = traverse[last].distance + neighbor->distance;
			if (traverse[neighbor->index].distance > distance)
			{
				traverse[neighbor->index].distance = distance;
				traverse[neighbor->index].origin = traverse + last;
				heap_emerge(&closest, traverse[neighbor->index].heap_index);
			}
			neighbor = neighbor->next;
		}

		last = heap_front(&closest) - traverse;
		if (traverse[last].distance == INFINITY) goto error;
		heap_pop(&closest);
	} while (last != node_target);

	free(closest.data);

	// Construct the final path by reversing the origin pointers.
	from = traverse + last;
	do
	{
		temp = from->origin;
		from->origin = from;
		from = temp;
	} while (from);

	// Add path points to move.
	temp = traverse + node_origin;
	do vector_add(moves, nodes->data + (temp - traverse));
	while (temp = temp->origin);

	free(traverse);

	return 0;

error:
	free(closest.data);
	free(traverse);
	return ERROR_MISSING;
}
