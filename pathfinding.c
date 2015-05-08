#include <string.h>

#include "types.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"

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
// TODO there must be obstacles applicable only for some players (gates can be opened only by owner's alliance)

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

// Checks whether the field target can be seen from origin (there are no obstacles in-between).
int path_visible(struct point origin, struct point target, const struct polygon *restrict obstacles, size_t obstacles_count)
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

static void graph_insert_angle(struct adjacency_list *nodes, struct point a, struct point b, struct point c)
{
	struct adjacency *node = &nodes->list[nodes->count++];
	node->neighbors = 0;

	// Calculate the coordinates of the inserted vertex.
	node->location.x = b.x + sign(b.x - a.x) + sign(b.x - c.x);
	node->location.y = b.y + sign(b.y - a.y) + sign(b.y - c.y);
}

static void graph_insert_end(struct adjacency_list *nodes, struct point start, struct point end)
{
	struct adjacency *node;

	// Calculate the coordinates of the inserted vertex.
	int direction_x = sign(end.x - start.x);
	int direction_y = sign(end.y - start.y);

	// Insert one node at each corner of the end point.
	if (direction_x)
	{
		node = &nodes->list[nodes->count++];
		node->neighbors = 0;
		node->location.x = end.x + direction_x;
		node->location.y = end.y + direction_y + 1;

		node = &nodes->list[nodes->count++];
		node->neighbors = 0;
		node->location.x = end.x + direction_x;
		node->location.y = end.y + direction_y - 1;
	}
	else // direction_y
	{
		node = &nodes->list[nodes->count++];
		node->neighbors = 0;
		node->location.x = end.x + direction_x + 1;
		node->location.y = end.y + direction_y;

		node = &nodes->list[nodes->count++];
		node->neighbors = 0;
		node->location.x = end.x + direction_x - 1;
		node->location.y = end.y + direction_y;
	}
}

// Attach the vertex at position index to the graph by adding the necessary edges.
static int graph_attach(struct adjacency_list *restrict nodes, size_t index, const struct polygon *restrict obstacles, size_t obstacles_count)
{
	size_t node;
	struct point from, to;
	struct neighbor *neighbor;
	double distance;

	for(node = 0; node < index; ++node)
	{
		from = nodes->list[index].location;
		to = nodes->list[node].location;
		if (path_visible(from, to, obstacles, obstacles_count))
		{
			distance = battlefield_distance(from, to);

			neighbor = malloc(sizeof(*neighbor));
			if (!neighbor) return ERROR_MEMORY;
			neighbor->index = node;
			neighbor->distance = distance;
			neighbor->next = nodes->list[index].neighbors;
			nodes->list[index].neighbors = neighbor;

			neighbor = malloc(sizeof(*neighbor));
			if (!neighbor) return ERROR_MEMORY;
			neighbor->index = index;
			neighbor->distance = distance;
			neighbor->next = nodes->list[node].neighbors;
			nodes->list[node].neighbors = neighbor;
		}
	}

	return 0;
}

// Detach the vertext at position index from the graph by removing the necessary edges.
static void graph_detach(struct adjacency_list *graph, size_t index)
{
	size_t i;
	struct neighbor *neighbor, *prev;

	// Remove node from neighbors lists.
	for(i = 0; i < index; ++i)
	{
		neighbor = graph->list[i].neighbors;
		prev = 0;
		while (neighbor)
		{
			if (neighbor->index == index)
			{
				if (prev)
				{
					prev->next = neighbor->next;
					free(neighbor);
					neighbor = prev->next;
				}
				else
				{
					graph->list[i].neighbors = neighbor->next;
					free(neighbor);
					neighbor = graph->list[i].neighbors;
				}
			}
			else
			{
				prev = neighbor;
				neighbor = prev->next;
			}
		}
	}

	// Remove the neighbors of the node.
	neighbor = graph->list[index].neighbors;
	while (neighbor)
	{
		prev = neighbor;
		neighbor = neighbor->next;
		free(prev);
	}
}

// Stores the vertices of the graph in nodes and returns the adjacency matrix of the graph.
struct adjacency_list *visibility_graph_build(const struct polygon *restrict obstacles, size_t obstacles_count)
{
	const struct polygon *restrict obstacle;
	size_t i, j;

	// Find the number of vertices in the visibility graph.
	struct adjacency_list *nodes;
	{
		size_t count = 0;
		for(i = 0; i < obstacles_count; ++i)
		{
			obstacle = obstacles + i;
			if (point_eq(obstacle->points[0], obstacle->points[obstacle->vertices_count - 1]))
				count += obstacle->vertices_count - 1;
			else
				count += obstacle->vertices_count + 2;
		}
		count += 2;

		nodes = malloc(sizeof(*nodes) + sizeof(*nodes->list) * count);
		if (!nodes) abort();
		nodes->count = 0;
	}

	// For each angle, use its exterior point for the visibility graph.
	for(i = 0; i < obstacles_count; ++i)
	{
		obstacle = obstacles + i;

		for(j = 2; j < obstacle->vertices_count; ++j)
			graph_insert_angle(nodes, obstacle->points[j - 2], obstacle->points[j - 1], obstacle->points[j]);

		if (point_eq(obstacle->points[0], obstacle->points[obstacle->vertices_count - 1])) // the obstacle is a closed loop
		{
			graph_insert_angle(nodes, obstacle->points[obstacle->vertices_count - 2], obstacle->points[0], obstacle->points[1]);
		}
		else
		{
			graph_insert_end(nodes, obstacle->points[1], obstacle->points[0]);
			graph_insert_end(nodes, obstacle->points[obstacle->vertices_count - 2], obstacle->points[obstacle->vertices_count - 1]);
		}
	}

	// Fill the adjacency list of the visibility graph.
	// Consider that no vertex is connected to itself.
	for(i = 1; i < nodes->count; ++i)
		if (graph_attach(nodes, i, obstacles, obstacles_count) < 0)
			goto error;

	// The adjacency list has place for two more vertices.
	// They will be used by pathfinding functions for origin and target vertices.

	return nodes;

error:
	visibility_graph_free(nodes);
	return 0;
}

void visibility_graph_free(struct adjacency_list *nodes)
{
	size_t i;
	struct neighbor *item, *next;

	for(i = 0; i < nodes->count; ++i)
	{
		item = nodes->list[i].neighbors;
		while (item)
		{
			next = item->next;
			free(item);
			item = next;
		}
	}
	free(nodes);
}

static ssize_t find_next(struct heap *restrict closest, struct path_node *restrict traverse_info, struct neighbor *restrict neighbor, size_t last)
{
	size_t next;

	// Update path from last to its neighbors.
	while (neighbor)
	{
		double distance = traverse_info[last].distance + neighbor->distance;
		if (distance < traverse_info[neighbor->index].distance)
		{
			traverse_info[neighbor->index].distance = distance;
			traverse_info[neighbor->index].origin = traverse_info + last;
			heap_emerge(closest, traverse_info[neighbor->index].heap_index);
		}
		neighbor = neighbor->next;
	}

	next = heap_front(closest) - traverse_info;
	if (traverse_info[next].distance == INFINITY) return ERROR_MISSING; // no more reachable vertices
	heap_pop(closest);

	return next;
}

int path_reachable(const struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct polygon *restrict obstacles, size_t obstacles_count, unsigned char reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	struct path_node *traverse_info;
	struct heap closest;
	size_t i;
	size_t x, y;
	size_t last;

	int status = 0;

	size_t vertex_origin;

	// Add vertex for the origin.
	vertex_origin = graph->count++;
	graph->list[vertex_origin].location = pawn->moves[pawn->moves_count - 1].location;
	graph->list[vertex_origin].neighbors = 0;
	graph_attach(graph, vertex_origin, obstacles, obstacles_count);

	// Initialize traversal information.
	traverse_info = malloc(graph->count * sizeof(*traverse_info));
	if (!traverse_info)
	{
		status = ERROR_MEMORY;
		goto finally;
	}
	for(i = 0; i < vertex_origin; ++i)
	{
		traverse_info[i].distance = INFINITY;
		traverse_info[i].origin = 0;
	}
	traverse_info[vertex_origin].distance = (pawn->moves[pawn->moves_count - 1].time * pawn->slot->unit->speed);
	traverse_info[vertex_origin].origin = 0;

	// Find the shortest path to each vertex using Dijkstra's algorithm.
	if (vertex_origin)
	{
		closest.count = vertex_origin;
		closest.data = malloc(closest.count * sizeof(traverse_info));
		if (!closest.data)
		{
			status = ERROR_MEMORY;
			goto finally;
		}
		for(i = 0; i < vertex_origin; ++i)
		{
			closest.data[i] = traverse_info + i;
			traverse_info[i].heap_index = i;
		}
		last = vertex_origin;
		do
		{
			last = find_next(&closest, traverse_info, graph->list[last].neighbors, last);
			if (last < 0) break;
		} while (closest.count);
		free(closest.data);
	}

	// Find which fields are visible from any of the graph vertices.
	// If such field can be reached in one round by the pawn, mark it as reachable.
	memset(reachable, 0, BATTLEFIELD_HEIGHT * BATTLEFIELD_WIDTH);
	// TODO check only the fields which are not farther than speed distance from the pawn
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
	{
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			for(i = 0; i < graph->count; ++i)
			{
				struct point target = {x, y};
				if (path_visible(graph->list[i].location, target, obstacles, obstacles_count))
					if (traverse_info[i].distance + battlefield_distance(graph->list[i].location, target) <= 1.0)
						reachable[y][x] = 1;
			}
		}
	}

finally:
	free(traverse_info);

	// Remove the vertex for origin.
	graph_detach(graph, vertex_origin);
	graph->count -= 1;

	return status;
}

// Finds path from the pawn's current final location to a target field. Appends the path to the pawn's movement queue.
// On error, returns error code and pawn movement queue remains unchanged.
int path_queue(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict graph, const struct polygon *restrict obstacles, size_t obstacles_count)
{
	struct path_node *traverse_info;
	struct heap closest;
	size_t i;
	size_t last;

	int status = 0;

	size_t vertex_origin, vertex_target;

	struct path_node *from, *next, *temp;
	// unsigned hops = 0;

	// Add vertex for the target.
	vertex_target = graph->count++;
	graph->list[vertex_target].location = target;
	graph->list[vertex_target].neighbors = 0;
	graph_attach(graph, vertex_target, obstacles, obstacles_count);

	// Add vertex for the origin.
	vertex_origin = graph->count++;
	graph->list[vertex_origin].location = pawn->moves[pawn->moves_count - 1].location;
	graph->list[vertex_origin].neighbors = 0;
	graph_attach(graph, vertex_origin, obstacles, obstacles_count);

	// Initialize traversal information.
	traverse_info = malloc(graph->count * sizeof(*traverse_info));
	if (!traverse_info)
	{
		status = ERROR_MEMORY;
		goto finally;
	}
	for(i = 0; i < vertex_origin; ++i)
	{
		traverse_info[i].distance = INFINITY;
		traverse_info[i].origin = 0;
	}
	traverse_info[vertex_origin].distance = (pawn->moves[pawn->moves_count - 1].time * pawn->slot->unit->speed); // TODO this is ugly
	traverse_info[vertex_origin].origin = 0;

	// Find the shortest path to target using Dijkstra's algorithm.
	closest.count = vertex_origin;
	// assert(closest.count);
	closest.data = malloc(closest.count * sizeof(traverse_info));
	if (!closest.data)
	{
		status = ERROR_MEMORY;
		goto finally;
	}
	for(i = 0; i < vertex_origin; ++i)
	{
		closest.data[i] = traverse_info + i;
		traverse_info[i].heap_index = i;
	}
	last = vertex_origin;
	while (1)
	{
		last = find_next(&closest, traverse_info, graph->list[last].neighbors, last);
		if (last == vertex_target) break;
		if ((last < 0) || !closest.count)
		{
			free(closest.data);
			status = ERROR_MISSING;
			goto finally;
		}
	}
	free(closest.data);

	// Construct the final path by reversing the origin pointers.
	from = traverse_info + vertex_target;
	next = 0;
	while (1)
	{
		temp = from->origin;
		from->origin = next;
		next = from;

		if (!temp) break; // vertex_origin found
		from = temp;

		// hops += 1;
	}

	// Add the selected path points to move.
	next = traverse_info + vertex_origin;
	while (next = next->origin)
	{
		double distance;
		pawn->moves[pawn->moves_count].location = graph->list[next - traverse_info].location;
		distance = battlefield_distance(pawn->moves[pawn->moves_count - 1].location, pawn->moves[pawn->moves_count].location);
		pawn->moves[pawn->moves_count].time = pawn->moves[pawn->moves_count - 1].time + distance / pawn->slot->unit->speed;
		pawn->moves_count += 1;
	}

finally:
	free(traverse_info);

	// Remove the vertices for origin and target.
	graph_detach(graph, vertex_origin);
	graph_detach(graph, vertex_target);
	graph->count -= 2;

	return status;
}
