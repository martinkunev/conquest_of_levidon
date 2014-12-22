#include "types.h"
#include "display.h"
#include "pathfinding.h"

struct path_node
{
	double distance;
	struct path_node *origin; // TODO ?rename to link
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
// The path consists of a sequence of straight lines with each intermediate point being at the corner of an obstacle.
// steps:
//  Build visibility graph with vertices: the corners of the obstacles, the origin point and the target point.
//  Use Dijkstra's algorithm to find the shortest path in the graph from origin to target.

// The obstacles are represented as a sequence of directed line segments (each one representing a wall).

// TODO polygons surrounding the blocked area. The coordinates are exclusive (limit fields are passable).
// TODO The fields are represented by their top left coordinates (in order to facilitate computations).

// TODO obstacles: rectangles, coastlines, fortresses
// TODO use bitmap to filter duplicated vertices
// TODO do I need non-symmetric distance?

// TODO there must be obstacles applicable only for some units (horses and balistas can not climb walls)
// TODO there must be obstacles applicable only for some players (doors can be opened by town owner's alliance)

static inline int sign(int number)
{
	return ((number > 0) - (number < 0));
}

static inline long cross_product(struct point f, struct point s)
{
	return (s.x * f.y - f.x * s.y); // negated because the coordinate system is clockwise (y axis is inverted)
}

// Checks if the wall (w0, w1) blocks the path (p0, p1)
static int blocks(struct point p0, struct point p1, struct point w0, struct point w1)
{
	// http://stackoverflow.com/questions/563198/how-do-you-detect-where-two-line-segments-intersect

	long cross;

	int direction_x = sign(w1.x - w0.x);
	int direction_y = sign(w1.y - w0.y);

	// Treat the wall as being located at the middle of the field.
	// Double the coordinates in order to use integers for the calculations.
	w0.x = w0.x * 2 - direction_y;
	w0.y = w0.y * 2 + direction_x;
	w1.x = w1.x * 2 - direction_y;
	w1.y = w1.y * 2 + direction_x;
	p0.x *= 2;
	p0.y *= 2;
	p1.x *= 2;
	p1.y *= 2;

	// Expand wall coordinates to span all the length of its boundary fields.
	w0.x -= direction_x;
	w0.y -= direction_y;
	w1.x += direction_x;
	w1.y += direction_y;

	// Express end coordinates as relative to their start coordinates.
	p1.x -= p0.x;
	p1.y -= p0.y;
	w1.x -= w0.x;
	w1.y -= w0.y;

	cross = cross_product(p1, w1);

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
		if ((0 <= wm) && (wm <= cross) && (0 <= pm) && (pm <= cross))
			return 1;
	}

	return 0;
}

// Checks whether the field target can be seen from origin.
static int visible(struct point origin, struct point target, const struct polygon *restrict obstacles, size_t obstacles_count)
{
	const struct polygon *restrict obstacle;
	size_t i, j;

	// Check if there is a wall that blocks the path from origin to target.
	for(i = 0; i < obstacles_count; ++i)
	{
		obstacle = obstacles + i;
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
			distance = battlefield_distance(from, to);

			neighbor = malloc(sizeof(*neighbor));
			if (!neighbor) return ERROR_MEMORY;
			neighbor->index = node;
			neighbor->distance = distance;
			neighbor->next = nodes->data[index].neighbors;
			nodes->data[index].neighbors = neighbor;

			neighbor = malloc(sizeof(*neighbor));
			if (!neighbor) return ERROR_MEMORY;
			neighbor->index = index;
			neighbor->distance = distance;
			neighbor->next = nodes->data[node].neighbors;
			nodes->data[node].neighbors = neighbor;
		}
	}

	return 0;
}

static int graph_insert_angle(struct vector_adjacency *nodes, struct point a, struct point b, struct point c)
{
	struct adjacency *node = vector_adjacency_insert(nodes);
	if (!node) return ERROR_MEMORY;
	node->neighbors = 0;

	// Calculate the coordinates of the inserted vertex.
	node->location.x = b.x + sign(b.x - a.x) + sign(b.x - c.x);
	node->location.y = b.y + sign(b.y - a.y) + sign(b.y - c.y);

	return 0;
}

static int graph_insert_end(struct vector_adjacency *nodes, struct point start, struct point end)
{
	struct adjacency *node;

	// Calculate the coordinates of the inserted vertex.
	int direction_x = sign(end.x - start.x);
	int direction_y = sign(end.y - start.y);

	// Insert one node at each corner of the end point.
	if (direction_x)
	{
		node = vector_adjacency_insert(nodes);
		if (!node) return ERROR_MEMORY;
		node->neighbors = 0;
		node->location.x = end.x + direction_x;
		node->location.y = end.y + direction_y + 1;

		node = vector_adjacency_insert(nodes);
		if (!node) return ERROR_MEMORY;
		node->neighbors = 0;
		node->location.x = end.x + direction_x;
		node->location.y = end.y + direction_y - 1;
	}
	else // direction_y
	{
		node = vector_adjacency_insert(nodes);
		if (!node) return ERROR_MEMORY;
		node->neighbors = 0;
		node->location.x = end.x + direction_x + 1;
		node->location.y = end.y + direction_y;

		node = vector_adjacency_insert(nodes);
		if (!node) return ERROR_MEMORY;
		node->neighbors = 0;
		node->location.x = end.x + direction_x - 1;
		node->location.y = end.y + direction_y;
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

	// For each angle, use its exterior point for the visibility graph.
	for(i = 0; i < obstacles_count; ++i)
	{
		obstacle = obstacles + i;

		for(j = 2; j < obstacle->vertices_count; ++j)
			if (graph_insert_angle(nodes, obstacle->points[j - 2], obstacle->points[j - 1], obstacle->points[j]) < 0)
				goto error;

		if (point_eq(obstacle->points[0], obstacle->points[obstacle->vertices_count - 1])) // the obstacle is a closed loop
		{
			if (graph_insert_angle(nodes, obstacle->points[obstacle->vertices_count - 2], obstacle->points[0], obstacle->points[1]) < 0)
				goto error;
		}
		else
		{
			if (graph_insert_end(nodes, obstacle->points[1], obstacle->points[0]) < 0)
				goto error;
			if (graph_insert_end(nodes, obstacle->points[obstacle->vertices_count - 2], obstacle->points[obstacle->vertices_count - 1]) < 0)
				goto error;
		}
	}

	// Fill the adjacency list of the visibility graph.
	// Consider that no vertex is connected to itself.
	for(i = 1; i < nodes->length; ++i)
		if (graph_attach(nodes, i, obstacles, obstacles_count) < 0)
			goto error;

	// Add dummy vertices for the start and the end nodes.
	vector_adjacency_insert(nodes);
	vector_adjacency_insert(nodes);

	// The vector will not be resized any more. Free the unused memory.
	vector_adjacency_shrink(nodes);

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

// Removes origin and target data left from previous calls to path_find().
static void graph_clean(struct vector_adjacency *restrict nodes)
{
	const size_t node_target = nodes->length - 2, node_origin = nodes->length - 1;

	size_t i;
	struct neighbor *neighbor, *prev;

	// Remove origin and target from neighbors lists.
	for(i = 0; i < node_target; ++i)
	{
		neighbor = nodes->data[i].neighbors;
		prev = 0;
		while (neighbor)
		{
			if ((neighbor->index == node_target) || (neighbor->index == node_origin))
			{
				if (prev)
				{
					prev->next = neighbor->next;
					free(neighbor);
					neighbor = prev->next;
				}
				else
				{
					nodes->data[i].neighbors = neighbor->next;
					free(neighbor);
					neighbor = nodes->data[i].neighbors;
				}
			}
			else
			{
				prev = neighbor;
				neighbor = prev->next;
			}
		}
	}

	// Remove the neighbors of origin and target.
	neighbor = nodes->data[node_target].neighbors;
	while (neighbor)
	{
		prev = neighbor;
		neighbor = neighbor->next;
		free(prev);
	}
	neighbor = nodes->data[node_origin].neighbors;
	while (neighbor)
	{
		prev = neighbor;
		neighbor = neighbor->next;
		free(prev);
	}
}

int path_find(struct queue *restrict moves, struct point target, struct vector_adjacency *restrict nodes, const struct polygon *restrict obstacles, size_t obstacles_count)
{
	const size_t node_origin = nodes->length - 1, node_target = nodes->length - 2;

	size_t i;
	size_t last;

	struct path_node *traverse = 0, *from, *next, *temp;
	struct heap closest = {0};

	struct neighbor *neighbor;
	double distance;
	unsigned hops;

	//struct point origin = *(struct point *)moves->data[moves->length - 1];
	struct point origin = moves->last->data.location;

	graph_clean(nodes);

	// Add target and origin points to the path graph.
	nodes->data[node_target].location = target;
	nodes->data[node_target].neighbors = 0;
	graph_attach(nodes, node_target, obstacles, obstacles_count);
	nodes->data[node_origin].location = origin;
	nodes->data[node_origin].neighbors = 0;
	graph_attach(nodes, node_origin, obstacles, obstacles_count);

	traverse = malloc(nodes->length * sizeof(*traverse));
	if (!traverse) return ERROR_MEMORY;

	closest.data = malloc((nodes->length - 1) * sizeof(traverse));
	if (!closest.data)
	{
		free(traverse);
		return ERROR_MEMORY;
	}

	// Build a heap from path nodes.
	for(i = 0; i < node_origin; ++i)
	{
		traverse[i].distance = INFINITY;
		traverse[i].origin = 0;
		traverse[i].heap_index = i;

		closest.data[i] = traverse + i;
	}
	closest.count = nodes->length - 1;
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

	// Construct the final path by reversing the origin pointers.
	from = traverse + last;
	next = 0;
	hops = 0;
	while (1)
	{
		temp = from->origin;
		from->origin = next;
		next = from;

		if (!temp) break;
		hops += 1;
		from = temp;
	}

	// Add the selected path points to move.
	temp = traverse + node_origin;
	/*if (vector_resize(moves, moves->length + hops) < 0) goto error;
	while (temp = temp->origin)
		moves->data[moves->length++] = nodes->data + (temp - traverse);*/
	while (temp = temp->origin)
	{
		struct move m;
		m.location = nodes->data[temp - traverse].location;
		// m.time is not initialized here
		m.distance = traverse[temp - traverse].distance;

		// TODO this is inefficient
		queue_push(moves, m); // TODO error check
	}

	free(closest.data);
	free(traverse);

	return 0;

error:
	free(closest.data);
	free(traverse);
	return ERROR_MISSING;
}
