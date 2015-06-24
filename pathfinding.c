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
#include "heap.g"

// Find the shortest path from an origin field to a target field.
// The path consists of a sequence of straight lines with each intermediate point being at the corner of an obstacle.
// steps:
//  Build visibility graph with vertices: the corners of the obstacles, the origin point and the target point.
//  Use Dijkstra's algorithm to find the shortest path in the graph from origin to target.

// Obstacles represent blocking objects, coastlines and fortresses on the battlefield.
// Each obstacle is represented as a line segment (start and end point).
// Only horizontal and vertical (not diagonal) obstacles are supported.
// Coordinates of the obstacles are inclusive (the end points are part of the obstacle).

// Internally, each field has size 2x2. Pawns are considered to be at the center of the corresponding field.
// The internal representation is used in obstacles and visibility graph.

// TODO there must be obstacles applicable only for some units (horses and balistas can not climb walls)
// TODO path_reachable should be refactored to not depend on battlefield size

static inline int sign(int number)
{
	return ((number > 0) - (number < 0));
}

static inline long cross_product(struct point f, struct point s)
{
	return (s.x * f.y - f.x * s.y); // negated because the coordinate system is clockwise (y axis is inverted)
}

static inline struct point field_position(struct point field)
{
	return (struct point){field.x * 2 + 1, field.y * 2 + 1};
}
static inline struct point position_field(struct point position)
{
	return (struct point){position.x / 2, position.y / 2};
}

static void obstacle_insert(struct obstacles *obstacles, unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
	obstacles->obstacle[obstacles->count].p[0] = (struct point){x0, y0};
	obstacles->obstacle[obstacles->count].p[1] = (struct point){x1, y1};
	obstacles->count += 1;
}

// Finds the obstacles on the battlefield. Constructs and returns a list of the obstacles.
struct obstacles *path_obstacles(const struct game *restrict game, const struct battle *restrict battle, unsigned char player)
{
	size_t x, y;

	struct obstacles *obstacles;
	size_t obstacles_count = 0;

	size_t i;
	int horizontal = -1; // start coordinate of the last detected horizontal wall
	int vertical[BATTLEFIELD_WIDTH]; // start coordinate of the last detected vertical walls
	for(i = 0; i < BATTLEFIELD_WIDTH; ++i) vertical[i] = -1;

	// Count the obstacles.
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
	{
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			const struct battlefield *field = &battle->field[y][x];
			if (field->blockage && !allies(game, field->owner, player) && !field->pawn)
			{
				if (horizontal >= 0)
				{
					if (field->position != (field->position | POSITION_LEFT | POSITION_RIGHT))
						obstacles_count += 1;
					if (!(field->position & POSITION_RIGHT)) horizontal = -1;
				}
				else
				{
					if (field->position & POSITION_RIGHT)
						horizontal = x * 2 + !(field->position & POSITION_LEFT);
					else if (field->position & POSITION_LEFT)
						obstacles_count += 1;
				}
				if (vertical[x] >= 0)
				{
					if (field->position != (field->position | POSITION_TOP | POSITION_BOTTOM))
						obstacles_count += 1;
					if (!(field->position & POSITION_BOTTOM)) vertical[x] = -1;
				}
				else
				{
					if (field->position & POSITION_BOTTOM)
						vertical[x] = y * 2 + !(field->position & POSITION_TOP);
					else if (field->position & POSITION_TOP)
						obstacles_count += 1;
				}
			}
			else
			{
				if (horizontal >= 0)
				{
					obstacles_count += 1;
					horizontal = -1;
				}
				if (vertical[x] >= 0)
				{
					obstacles_count += 1;
					vertical[x] = -1;
				}
			}
		}

		if (horizontal >= 0)
		{
			obstacles_count += 1;
			horizontal = -1;
		}
	}
	for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
	{
		if (vertical[x] >= 0)
		{
			obstacles_count += 1;
			vertical[x] = -1;
		}
	}

	obstacles = malloc(sizeof(*obstacles) + obstacles_count * sizeof(*obstacles->obstacle));
	if (!obstacles) return 0;
	obstacles->count = 0;

	// Insert the obstacles.
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
	{
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			const struct battlefield *field = &battle->field[y][x];
			if (field->blockage && !allies(game, field->owner, player) && !field->pawn)
			{
				if (horizontal >= 0)
				{
					if (field->position != (field->position | POSITION_LEFT | POSITION_RIGHT))
						obstacle_insert(obstacles, horizontal, y * 2 + 1, x * 2 + ((field->position & POSITION_LEFT) != 0), y * 2 + 1);
					if (!(field->position & POSITION_RIGHT)) horizontal = -1;
				}
				else
				{
					if (field->position & POSITION_RIGHT)
						horizontal = x * 2 + !(field->position & POSITION_LEFT);
					else if (field->position & POSITION_LEFT)
						obstacle_insert(obstacles, x * 2, y * 2 + 1, x * 2 + 1, y * 2 + 1);
				}
				if (vertical[x] >= 0)
				{
					if (field->position != (field->position | POSITION_TOP | POSITION_BOTTOM))
						obstacle_insert(obstacles, x * 2 + 1, vertical[x], x * 2 + 1, y * 2 + ((field->position & POSITION_TOP) != 0));
					if (!(field->position & POSITION_BOTTOM)) vertical[x] = -1;
				}
				else
				{
					if (field->position & POSITION_BOTTOM)
						vertical[x] = y * 2 + !(field->position & POSITION_TOP);
					else if (field->position & POSITION_TOP)
						obstacle_insert(obstacles, x * 2 + 1, y * 2, x * 2 + 1, y * 2 + 1);
				}
			}
			else
			{
				if (horizontal >= 0)
				{
					obstacle_insert(obstacles, horizontal, y * 2 + 1, x * 2, y * 2 + 1);
					horizontal = -1;
				}
				if (vertical[x] >= 0)
				{
					obstacle_insert(obstacles, x * 2 + 1, vertical[x], x * 2 + 1, y * 2);
					vertical[x] = -1;
				}
			}
		}

		if (horizontal >= 0)
		{
			obstacle_insert(obstacles, horizontal, y * 2 + 1, BATTLEFIELD_WIDTH * 2, y * 2 + 1);
			horizontal = -1;
		}
	}
	for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
	{
		if (vertical[x] >= 0)
		{
			obstacle_insert(obstacles, x * 2 + 1, vertical[x], x * 2 + 1, BATTLEFIELD_HEIGHT * 2);
			vertical[x] = -1;
		}
	}

	return obstacles;
}

// Checks if the wall (w0, w1) blocks the path (p0, p1)
static int blocks(struct point p0, struct point p1, struct point w0, struct point w1)
{
	// http://stackoverflow.com/questions/563198/how-do-you-detect-where-two-line-segments-intersect

	long cross;

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
		// TODO is this good?
		//if ((0 <= wm) && (wm <= cross) && (0 <= pm) && (pm <= cross))
		if ((0 < wm) && (wm < cross) && (0 < pm) && (pm < cross))
			return 1;
	}

	return 0;
}

// Checks whether the field target can be seen from origin (there are no obstacles in-between).
static int visible(struct point origin, struct point target, const struct obstacles *restrict obstacles)
{
	size_t i;

	// Check if there is a wall that blocks the path from origin to target.
	for(i = 0; i < obstacles->count; ++i)
	{
		const struct obstacle *restrict obstacle = obstacles->obstacle + i;
		if (blocks(origin, target, obstacle->p[0], obstacle->p[1]))
			return 0;
	}

	return 1;
}

int path_visible(struct point origin, struct point target, const struct obstacles *restrict obstacles)
{
	return visible(field_position(origin), field_position(target), obstacles);
}

// Attach the vertex at position index to the graph by adding the necessary edges.
static int graph_attach(struct adjacency_list *restrict graph, size_t index, const struct obstacles *restrict obstacles)
{
	size_t node;
	struct point from, to;
	struct neighbor *neighbor;
	double distance;

	from = graph->list[index].location;

	for(node = 0; node < index; ++node)
	{
		to = graph->list[node].location;
		if (visible(from, to, obstacles))
		{
			distance = battlefield_distance(from, to);

			neighbor = malloc(sizeof(*neighbor));
			if (!neighbor) return ERROR_MEMORY;
			neighbor->index = node;
			neighbor->distance = distance;
			neighbor->next = graph->list[index].neighbors;
			graph->list[index].neighbors = neighbor;

			neighbor = malloc(sizeof(*neighbor));
			if (!neighbor) return ERROR_MEMORY;
			neighbor->index = index;
			neighbor->distance = distance;
			neighbor->next = graph->list[node].neighbors;
			graph->list[node].neighbors = neighbor;
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

static void vertex_insert(struct adjacency_list *graph, const struct obstacle *restrict obstacle, int x, int y, unsigned char occupied[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	struct adjacency *node;

	int field_x = x / 2;
	int field_y = y / 2;

	// Don't insert a vertex if the coordinates are outside the battlefield.
	if ((field_x < 0) || (field_x >= BATTLEFIELD_WIDTH) || (field_y < 0) || (field_y >= BATTLEFIELD_HEIGHT))
		return;

	// Don't insert a vertex if the field is occupied.
	if (occupied[field_y][field_x])
		return;

	occupied[field_y][field_x] = 1;

	node = &graph->list[graph->count++];
	node->neighbors = 0;
	node->location = (struct point){x, y};
}

// Returns the visibility graph as adjacency list.
struct adjacency_list *visibility_graph_build(const struct battle *restrict battle, const struct obstacles *restrict obstacles)
{
	unsigned char occupied[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH] = {0};

	size_t i, j;

	struct adjacency_list *graph, *graph_resized;

	for(i = 0; i < BATTLEFIELD_HEIGHT; ++i)
		for(j = 0; j < BATTLEFIELD_WIDTH; ++j)
			if (battle->field[i][j].blockage)
				occupied[i][j] = 1;

	// Allocate enough memory for the maximum size of the adjacency graph.
	// 4 vertices for the corners around each obstacle and 2 vertices for origin and target
	graph = malloc(sizeof(*graph) + sizeof(*graph->list) * (obstacles->count * 4 + 2));
	if (!graph) return 0;
	graph->count = 0;

	for(i = 0; i < obstacles->count; ++i)
	{
		const struct obstacle *restrict obstacle = obstacles->obstacle + i;

		int direction_x = sign(obstacle->p[1].x - obstacle->p[0].x);
		int direction_y = sign(obstacle->p[1].y - obstacle->p[0].y);

		// Insert one node at each corner of each end point.
		if (direction_x)
		{
			int direction_x0 = ((obstacle->p[0].x % 2) ? (direction_x * 2) : direction_x);
			int direction_x1 = ((obstacle->p[1].x % 2) ? (direction_x * 2) : direction_x);
			vertex_insert(graph, obstacle, obstacle->p[0].x - direction_x0, obstacle->p[0].y - 2, occupied);
			vertex_insert(graph, obstacle, obstacle->p[0].x - direction_x0, obstacle->p[0].y + 2, occupied);
			vertex_insert(graph, obstacle, obstacle->p[1].x + direction_x1, obstacle->p[1].y - 2, occupied);
			vertex_insert(graph, obstacle, obstacle->p[1].x + direction_x1, obstacle->p[1].y + 2, occupied);
		}
		else // direction_y
		{
			int direction_y0 = ((obstacle->p[0].y % 2) ? (direction_y * 2) : direction_y);
			int direction_y1 = ((obstacle->p[1].y % 2) ? (direction_y * 2) : direction_y);
			vertex_insert(graph, obstacle, obstacle->p[0].x - 2, obstacle->p[0].y - direction_y0, occupied);
			vertex_insert(graph, obstacle, obstacle->p[0].x + 2, obstacle->p[0].y - direction_y0, occupied);
			vertex_insert(graph, obstacle, obstacle->p[1].x - 2, obstacle->p[1].y + direction_y1, occupied);
			vertex_insert(graph, obstacle, obstacle->p[1].x + 2, obstacle->p[1].y + direction_y1, occupied);
		}
	}

	// Free the unused part of the adjacency graph buffer.
	// Leave space for origin and target vertices.
	graph_resized = realloc(graph, sizeof(*graph) + sizeof(*graph->list) * (graph->count + 2));
	if (!graph_resized) graph_resized = graph;

	// Fill the adjacency list of the visibility graph.
	// Consider that no vertex is connected to itself.
	for(i = 1; i < graph_resized->count; ++i)
		if (graph_attach(graph_resized, i, obstacles) < 0)
		{
			visibility_graph_free(graph_resized);
			return 0;
		}

	// The origin and target vertices will be set by pathfinding functions.
	return graph_resized;
}

void visibility_graph_free(struct adjacency_list *graph)
{
	size_t i;
	struct neighbor *item, *next;

	for(i = 0; i < graph->count; ++i)
	{
		item = graph->list[i].neighbors;
		while (item)
		{
			next = item->next;
			free(item);
			item = next;
		}
	}
	free(graph);
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

int path_reachable(const struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, unsigned char reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	struct path_node *traverse_info;
	struct heap closest;
	size_t i;
	size_t x, y;
	ssize_t last;

	int status = 0;

	size_t vertex_origin;

	// TODO don't treat blockage positions as reachable (they are allowed on some diagonals for some reason)

	// Add vertex for the origin.
	vertex_origin = graph->count++;
	graph->list[vertex_origin].location = field_position(pawn->moves[pawn->moves_count - 1].location);
	graph->list[vertex_origin].neighbors = 0;
	graph_attach(graph, vertex_origin, obstacles);

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
	traverse_info[vertex_origin].distance = (pawn->moves[pawn->moves_count - 1].time * pawn->troop->unit->speed);
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
				if (visible(graph->list[i].location, field_position(target), obstacles))
				{
					struct point field = position_field(graph->list[i].location);
					if (traverse_info[i].distance + battlefield_distance(field, target) <= pawn->troop->unit->speed)
						reachable[y][x] = 1;
				}
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
int path_queue(struct pawn *restrict pawn, struct point target, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	struct path_node *traverse_info;
	struct heap closest;
	size_t i;
	ssize_t last;

	int status = 0;

	size_t vertex_origin, vertex_target;

	struct path_node *from, *next, *temp;

	// Add vertex for the target.
	vertex_target = graph->count++;
	graph->list[vertex_target].location = field_position(target);
	graph->list[vertex_target].neighbors = 0;
	graph_attach(graph, vertex_target, obstacles);

	// Add vertex for the origin.
	vertex_origin = graph->count++;
	graph->list[vertex_origin].location = field_position(pawn->moves[pawn->moves_count - 1].location);
	graph->list[vertex_origin].neighbors = 0;
	graph_attach(graph, vertex_origin, obstacles);

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
	traverse_info[vertex_origin].distance = (pawn->moves[pawn->moves_count - 1].time * pawn->troop->unit->speed); // TODO this is ugly
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
	}

	// Add the selected path points to move.
	next = traverse_info + vertex_origin;
	while (next = next->origin)
	{
		double distance;
		pawn->moves[pawn->moves_count].location = position_field(graph->list[next - traverse_info].location);
		distance = battlefield_distance(pawn->moves[pawn->moves_count - 1].location, pawn->moves[pawn->moves_count].location);
		pawn->moves[pawn->moves_count].time = pawn->moves[pawn->moves_count - 1].time + distance / pawn->troop->unit->speed;
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
