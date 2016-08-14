/*
 * Conquest of Levidon
 * Copyright (C) 2016  Martin Kunev <martinkunev@gmail.com>
 *
 * This file is part of Conquest of Levidon.
 *
 * Conquest of Levidon is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 3 of the License.
 *
 * Conquest of Levidon is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Conquest of Levidon.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "game.h"
#include "pathfinding.h"
#include "movement.h"
#include "battle.h"

#define FLOAT_ERROR 0.001

struct path_node
{
	double distance;
	struct path_node *path_link;
	size_t heap_index;
};

#define heap_type struct path_node *
#define heap_above(a, b) ((a)->distance <= (b)->distance)
#define heap_update(heap, position) ((heap)->data[position]->heap_index = (position))
#include "generic/heap.g"

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

static inline int sign(int number)
{
	return ((number > 0) - (number < 0));
}

// Determines the relative position of a point and a line described by a vector.
// Returns 1 if the point is on the right side, -1 if the point is on the left side and 0 if the point is on the line.
static int point_side(struct position p, struct position v0, struct position v1)
{
	float value = (v1.y - v0.y) * p.x + (v0.x - v1.x) * p.y + v1.x * v0.y - v0.x * v1.y;
	if (value > 0) return 1;
	else if (value < 0) return -1;
	else return 0;
}

// Determines whether a move is blocked by an obstacle.
// The obstacle doesn't block moves right next to it ("touching" the obstacle is allowed).
static int move_blocked_obstacle(struct position start, struct position end, const struct obstacle *restrict obstacle)
{
	// http://stackoverflow.com/a/293052/515212

	int side;
	int sides[3] = {0};

	// Determine relative position of the right-top corner of the obstacle.
	side = point_side((struct position){obstacle->right, obstacle->top}, start, end);
	sides[side + 1] = 1;

	// Determine relative position of the left-top corner of the obstacle.
	side = point_side((struct position){obstacle->left, obstacle->top}, start, end);
	sides[side + 1] = 1;

	// Determine relative position of the left-bottom corner of the obstacle.
	side = point_side((struct position){obstacle->left, obstacle->bottom}, start, end);
	sides[side + 1] = 1;

	// Determine relative position of the left-top corner of the obstacle.
	side = point_side((struct position){obstacle->right, obstacle->bottom}, start, end);
	sides[side + 1] = 1;

	if (sides[0] && sides[2]) // if there are corners on both sides of the path
	{
		if ((start.x >= obstacle->right) && (end.x >= obstacle->right)) return 0;
		if ((start.x <= obstacle->left) && (end.x <= obstacle->left)) return 0;
		if ((start.y <= obstacle->top) && (end.y <= obstacle->top)) return 0;
		if ((start.y >= obstacle->bottom) && (end.y >= obstacle->bottom)) return 0;

		return 1;
	}

	return 0;
}

// WARNING: The move must be a non-zero vector (end != start).
// TODO do I need this function
static int move_blocked_pawn(struct position start, struct position end, struct position pawn)
{
	// http://stackoverflow.com/a/1084899/515212

	const double radius = PAWN_RADIUS * 2;

	// Change the coordinate system so that the path starts from (0, 0).
	end.x -= start.x;
	end.y -= start.y;
	pawn.x -= start.x;
	pawn.y -= start.y;

	double a = end.x * end.x + end.y * end.y;
	double b = -2 * (pawn.x * end.x + pawn.y * end.y);
	double c = pawn.x * pawn.x + pawn.y * pawn.y - radius * radius;
	double discriminant = b * b - 4 * a * c;

	if (discriminant < 0) return 0;

	discriminant = sqrt(discriminant);
	double solution0 = (- b - discriminant) / (2 * a);
	double solution1 = (- b + discriminant) / (2 * a);

	if ((solution0 >= 0) && (solution0 <= 1)) return 1;
	if ((solution1 >= 0) && (solution1 <= 1)) return 1;

	return 0;
}

static inline void obstacle_insert(struct obstacles *obstacles, float left, float right, float top, float bottom)
{
	obstacles->obstacle[obstacles->count] = (struct obstacle){left, right, top, bottom};
	obstacles->count += 1;
}

// Finds the obstacles on the battlefield. Constructs and returns a list of the obstacles.
struct obstacles *path_obstacles_alloc(const struct game *restrict game, const struct battle *restrict battle, unsigned char player)
{
	size_t x, y;

	struct obstacles *obstacles;
	size_t obstacles_count = 0;

	size_t i;
	float horizontal = -1; // start coordinate of the last detected horizontal wall
	float vertical[BATTLEFIELD_WIDTH]; // start coordinate of the last detected vertical walls
	for(i = 0; i < BATTLEFIELD_WIDTH; ++i)
		vertical[i] = -1;

	// Count the obstacles.
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
	{
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			const struct battlefield *field = &battle->field[y][x];
			unsigned char blockage_location = (battlefield_passable(game, field, player) ? 0 : field->blockage_location);

			if (horizontal >= 0)
			{
				if (blockage_location != (blockage_location | POSITION_LEFT | POSITION_RIGHT))
				{
					obstacles_count += 1;
					horizontal = (blockage_location & POSITION_RIGHT) ? x : -1.0;
				}
			}
			else
			{
				if (blockage_location & POSITION_RIGHT)
					horizontal = x;
				else if (blockage_location & POSITION_LEFT)
					obstacles_count += 1;
			}
			if (vertical[x] >= 0)
			{
				if (blockage_location != (blockage_location | POSITION_TOP | POSITION_BOTTOM))
				{
					obstacles_count += 1;
					vertical[x] = (blockage_location & POSITION_BOTTOM) ? y : -1.0;
				}
			}
			else
			{
				if (blockage_location & POSITION_BOTTOM)
					vertical[x] = y;
				else if (blockage_location & POSITION_TOP)
					obstacles_count += 1;
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
			unsigned char blockage_location = (battlefield_passable(game, field, player) ? 0 : field->blockage_location);

			if (horizontal >= 0)
			{
				if (blockage_location != (blockage_location | POSITION_LEFT | POSITION_RIGHT))
				{
					obstacle_insert(obstacles, horizontal, x + (1 - WALL_OFFSET) * ((blockage_location & POSITION_LEFT) != 0), y + WALL_OFFSET, y + (1 - WALL_OFFSET));
					horizontal = (blockage_location & POSITION_RIGHT) ? (x + WALL_OFFSET) : -1.0;
				}
			}
			else
			{
				if (blockage_location & POSITION_RIGHT)
					horizontal = x + WALL_OFFSET * !(blockage_location & POSITION_LEFT);
				else if (blockage_location & POSITION_LEFT)
					obstacle_insert(obstacles, x, x + (1 - WALL_OFFSET), y + WALL_OFFSET, y + (1 - WALL_OFFSET));
			}
			if (vertical[x] >= 0)
			{
				if (blockage_location != (blockage_location | POSITION_TOP | POSITION_BOTTOM))
				{
					obstacle_insert(obstacles, x + WALL_OFFSET, x + (1 - WALL_OFFSET), vertical[x], y + (1 - WALL_OFFSET) * ((blockage_location & POSITION_TOP) != 0));
					vertical[x] = (blockage_location & POSITION_BOTTOM) ? (y + WALL_OFFSET) : -1.0;
				}
			}
			else
			{
				if (blockage_location & POSITION_BOTTOM)
					vertical[x] = y + WALL_OFFSET * !(blockage_location & POSITION_TOP);
				else if (blockage_location & POSITION_TOP)
					obstacle_insert(obstacles, x + WALL_OFFSET, x + (1 - WALL_OFFSET), y, y + (1 - WALL_OFFSET));
			}
		}

		if (horizontal >= 0)
		{
			obstacle_insert(obstacles, horizontal, BATTLEFIELD_WIDTH, y + WALL_OFFSET, y + (1 - WALL_OFFSET));
			horizontal = -1;
		}
	}
	for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
	{
		if (vertical[x] >= 0)
		{
			obstacle_insert(obstacles, x + WALL_OFFSET, x + (1 - WALL_OFFSET), vertical[x], BATTLEFIELD_HEIGHT);
			vertical[x] = -1;
		}
	}

	return obstacles;
}

// Checks whether a pawn can see and move directly from origin to target (there are no obstacles in-between).
int path_visible(struct position origin, struct position target, const struct obstacles *restrict obstacles)
{
	// Check if there is an obstacle that blocks the path from origin to target.
	for(size_t i = 0; i < obstacles->count; ++i)
		if (move_blocked_obstacle(origin, target, obstacles->obstacle + i))
			return 0;

	return 1;
}

// Attach the vertex at position index to the graph by adding the necessary edges.
static int graph_vertex_attach(struct adjacency_list *restrict graph, size_t index, const struct obstacles *restrict obstacles)
{
	size_t node;
	struct position from, to;

	from = graph->list[index].position;

	for(node = 0; node < index; ++node)
	{
		to = graph->list[node].position;
		if (path_visible(from, to, obstacles))
		{
			struct neighbor *neighbor;
			double distance = battlefield_distance(from, to);

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

static void graph_vertex_add(struct adjacency_list *restrict graph, float x, float y, unsigned char occupied[static BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	struct adjacency *node;

	// Don't insert a vertex that is outside the battlefield.
	if ((x < 0) || (x > BATTLEFIELD_WIDTH) || (y < 0) || (y > BATTLEFIELD_HEIGHT))
		return;

	// Don't insert a vertex if the field is occupied.
	if (occupied[(size_t)y][(size_t)x])
		return;
	occupied[(size_t)y][(size_t)x] = 1;

	node = &graph->list[graph->count++];
	node->neighbors = 0;
	node->position = (struct position){x, y};
}

// Returns the visibility graph as adjacency list.
struct adjacency_list *visibility_graph_build(const struct battle *restrict battle, const struct obstacles *restrict obstacles, unsigned vertices_reserved)
{
	size_t i, j;

	struct adjacency_list *graph, *graph_resized;

	unsigned char occupied[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH] = {0};
	for(i = 0; i < BATTLEFIELD_HEIGHT; ++i)
		for(j = 0; j < BATTLEFIELD_WIDTH; ++j)
			if (battle->field[i][j].blockage)
				occupied[i][j] = 1;

	// Allocate enough memory for the maximum size of the adjacency graph.
	// 4 vertices for the corners around each obstacle
	graph = malloc(offsetof(struct adjacency_list, list) + sizeof(*graph->list) * (obstacles->count * 4 + vertices_reserved));
	if (!graph) return 0;
	graph->count = 0;

	for(i = 0; i < obstacles->count; ++i)
	{
		const struct obstacle *restrict obstacle = obstacles->obstacle + i;
		graph_vertex_add(graph, obstacle->left - PAWN_RADIUS, obstacle->bottom + PAWN_RADIUS, occupied);
		graph_vertex_add(graph, obstacle->right + PAWN_RADIUS, obstacle->bottom + PAWN_RADIUS, occupied);
		graph_vertex_add(graph, obstacle->right + PAWN_RADIUS, obstacle->top - PAWN_RADIUS, occupied);
		graph_vertex_add(graph, obstacle->left - PAWN_RADIUS, obstacle->top - PAWN_RADIUS, occupied);
	}

	// Free the unused part of the adjacency graph buffer.
	// Leave space for origin and target vertices.
	graph_resized = realloc(graph, offsetof(struct adjacency_list, list) + sizeof(*graph->list) * (graph->count + vertices_reserved));
	if (!graph_resized) graph_resized = graph;

	// Fill the adjacency list of the visibility graph.
	// Consider that no vertex is connected to itself.
	for(i = 1; i < graph_resized->count; ++i)
		if (graph_vertex_attach(graph_resized, i, obstacles) < 0)
		{
			visibility_graph_free(graph_resized);
			return 0;
		}

	// The origin and target vertices will be set by pathfinding functions.
	return graph_resized;
}

void visibility_graph_free(struct adjacency_list *graph)
{
	struct neighbor *item, *next;

	if (!graph) return;

	for(size_t i = 0; i < graph->count; ++i)
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

// WARNING: The space for the new vertex must have been reserved by visibility_graph_build().
static ssize_t graph_insert(struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, struct position position)
{
	int status;

	size_t index = graph->count++;
	struct adjacency *node = &graph->list[index];

	node->neighbors = 0;
	node->position = position;

	status = graph_vertex_attach(graph, index, obstacles);
	if (status < 0) return status;

	return index;
}

// Removes the vertex at position index from the graph.
static void graph_remove(struct adjacency_list *restrict graph, size_t index)
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

	graph->count -= 1;
}

static ssize_t find_closest(struct heap *restrict closest, struct path_node *restrict traverse_info, struct neighbor *restrict neighbor, size_t last)
{
	size_t next;

	// Update path from last to its neighbors.
	while (neighbor)
	{
		double distance = traverse_info[last].distance + neighbor->distance;
		if (distance < traverse_info[neighbor->index].distance)
		{
			traverse_info[neighbor->index].distance = distance;
			traverse_info[neighbor->index].path_link = traverse_info + last;
			heap_emerge(closest, traverse_info[neighbor->index].heap_index);
		}
		neighbor = neighbor->next;
	}

	next = closest->data[0] - traverse_info;
	if (traverse_info[next].distance == INFINITY) return ERROR_MISSING; // no more reachable vertices
	heap_pop(closest);

	return next;
}

// Traverses the graph using Dijkstra's algorithm.
// Stops when it finds path to vertex_target or all vertices reachable from origin are traversed (and no such path is found).
// Returns information about the paths found or NULL on memory error.
// WARNING: The last vertex in the graph must be the origin.
static struct path_node *path_traverse(struct adjacency_list *restrict graph, size_t vertex_target)
{
	struct path_node *traverse_info;
	struct heap closest;
	size_t i;
	ssize_t vertex;

	assert(graph->count);

	size_t vertex_origin = graph->count - 1;

	// Initialize traversal information.
	traverse_info = malloc(graph->count * sizeof(*traverse_info));
	if (!traverse_info)
		goto finally; // memory error
	for(i = 0; i < vertex_origin; ++i)
	{
		traverse_info[i].distance = INFINITY;
		traverse_info[i].path_link = 0;
	}
	traverse_info[vertex_origin].distance = 0;
	traverse_info[vertex_origin].path_link = 0;

	// Find the shortest path to target using Dijkstra's algorithm.
	if (vertex_origin)
	{
		closest.count = vertex_origin;
		closest.data = malloc(closest.count * sizeof(traverse_info));
		if (!closest.data)
		{
			free(traverse_info);
			traverse_info = 0;
			goto finally; // memory error
		}

		for(i = 0; i < vertex_origin; ++i)
		{
			closest.data[i] = traverse_info + i;
			traverse_info[i].heap_index = i;
		}

		vertex = vertex_origin;
		do
		{
			vertex = find_closest(&closest, traverse_info, graph->list[vertex].neighbors, vertex);
			if (vertex == vertex_target) break; // found
			if (vertex < 0) break; // no more reachable vertices
		} while (closest.count);
		free(closest.data);
	}

finally:
	return traverse_info;
}

// Finds path from the pawn's current position to destination and stores it in pawn->moves.
// On error, returns error code and pawn movement queue remains unchanged.
int path_find(struct pawn *restrict pawn, struct position destination, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	ssize_t vertex_target, vertex_origin;
	struct path_node *traverse_info;
	int status;

	struct path_node *node, *prev, *next;
	size_t moves_count;

	vertex_target = graph_insert(graph, obstacles, destination);
	if (vertex_target < 0) return vertex_target;

	vertex_origin = graph_insert(graph, obstacles, pawn->position);
	if (vertex_origin < 0)
	{
		graph_remove(graph, vertex_target);
		return vertex_origin;
	}

	// Look for a path from the pawn's position to the target vertex.
	traverse_info = path_traverse(graph, vertex_target);
	if (!traverse_info)
	{
		status = ERROR_MEMORY;
		goto finally;
	}
	if (traverse_info[vertex_target].distance == INFINITY)
	{
		status = ERROR_MISSING;
		goto finally;
	}

	// Construct the path to target by starting from target and reversing the path_link pointers.
	node = traverse_info + vertex_target;
	next = 0;
	moves_count = 0;
	while (1)
	{
		prev = node->path_link;
		node->path_link = next;
		next = node;

		if (!prev) break; // there is no previous node, so node is the origin
		node = prev;

		moves_count += 1;
	}

	// Add the selected path points to the movement queue.
	pawn->moves.count = 0;
	array_moves_expand(&pawn->moves, moves_count);
	while (node = node->path_link)
	{
		pawn->moves.data[pawn->moves.count] = graph->list[node - traverse_info].position;
		pawn->moves.count += 1;
	}

	status = 0;

finally:
	free(traverse_info);
	graph_remove(graph, vertex_origin);
	graph_remove(graph, vertex_target);
	return status;
}

// Returns the shortest distance between the pawn and destination. On error, returns NAN.
double path_distance(struct pawn *restrict pawn, struct position destination, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	ssize_t vertex_target, vertex_origin;
	struct path_node *traverse_info;
	double result;

	vertex_target = graph_insert(graph, obstacles, destination);
	if (vertex_target < 0) return NAN;

	vertex_origin = graph_insert(graph, obstacles, pawn->position);
	if (vertex_origin < 0)
	{
		graph_remove(graph, vertex_target);
		return NAN;
	}

	// Look for a path from the pawn's position to the target vertex.
	traverse_info = path_traverse(graph, vertex_target);
	result = traverse_info ? traverse_info[vertex_target].distance : NAN;

finally:
	free(traverse_info);
	graph_remove(graph, vertex_origin);
	graph_remove(graph, vertex_target);
	return result;
}

int path_distances(const struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, double reachable[static BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	ssize_t vertex_origin;
	struct path_node *traverse_info;
	int status;

	// TODO maybe use pawn->path.data[pawn->path.count - 1] for start vertex
	vertex_origin = graph_insert(graph, obstacles, pawn->position);
	if (vertex_origin < 0) return vertex_origin;

	// Look for a path from the pawn's position to a non-existent target (so that all vertices are traversed).
	traverse_info = path_traverse(graph, graph->count);
	if (!traverse_info)
	{
		status = ERROR_MEMORY;
		goto finally;
	}

	// Find which tiles are visible from any graph vertex.
	// Store the least distance to each visible tile.
	// TODO this is slow; maybe use a queue of tiles which to visit and check only the distance to them (if a tile is reachable, add its neighbors to the queue)
	for(size_t y = 0; y < BATTLEFIELD_HEIGHT; ++y)
	{
		for(size_t x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			struct position target = {x + 0.5, y + 0.5};
			reachable[y][x] = INFINITY;
			for(size_t i = 0; i < graph->count; ++i)
			{
				double distance = traverse_info[i].distance;
				if ((distance < INFINITY) && path_visible(graph->list[i].position, target, obstacles))
				{
					distance += battlefield_distance(graph->list[i].position, target);
					if (distance < reachable[y][x]) reachable[y][x] = distance;
				}
			}
		}
	}

	status = 0;

finally:
	free(traverse_info);
	graph_remove(graph, vertex_origin);
	return status;
}

// Sets a move with the specified distance in the direction of the specified point, if that direction does not oppose the original pawn direction.
static unsigned pawn_move_set(struct position *restrict move, const struct pawn *restrict pawn, double distance, double x, double y, struct position origin)
{
	double old_x = pawn->position_next.x - pawn->position.x;
	double old_y = pawn->position_next.y - pawn->position.y;

	// Change movement direction away from the obstacle to prevent rounding error leading to collision.
	x *= 1 + FLOAT_ERROR;
	y *= 1 + FLOAT_ERROR;

	// Determine movement direction vector.
	x -= origin.x;
	y -= origin.y;

	// Use the sign of the dot product to determine whether the angle between the old and the new direction is less than 90 degrees.
	if (x * old_x + y * old_y > -FLOAT_ERROR)
	{
		double length = sqrt(x * x + y * y);
		x = pawn->position.x + x * distance / length;
		y = pawn->position.y + y * distance / length;

		if (in_battlefield(x, y))
		{
			move->x = x;
			move->y = y;
			return 1;
		}

		return 0;
	}

	return 0;
}

// Initializes the possible one-step moves for the given pawn in a tangent direction to the obstacle pawn.
unsigned path_moves_tangent(const struct pawn *restrict pawn, const struct pawn *restrict obstacle, double distance_covered, struct position moves[static restrict 2])
{
	unsigned moves_count = 0;

	const double r = PAWN_RADIUS * 2; // the two pawns must not overlap
	const double r2 = r * r;

	// Find a point (x, y) on the tangent that is at distance r from the tangent point. To do that use the following relations:
	// The points (x, y), the center of the obstacle circle and the tangent point form an isosceles right triangle.
	// The oigin point, the tangent point and (x, y) lie on the tangent. The tangent point is between the other two.

	double x, y;
	double distance2, distance_tangent_point, discriminant, minus_b, temp;

	// Change the coordinate system so that the obstacle circle is at the origin.
	struct position origin = pawn->position;
	origin.x -= obstacle->position.x;
	origin.y -= obstacle->position.y;

	distance2 = origin.x * origin.x + origin.y * origin.y;
	distance_tangent_point = ((distance2 >= r2) ? sqrt(distance2 - r2) : 0); // handle the case when the argument is negative due to rounding errors

	// Solutions:
	//  x = (r2 * origin.x - r * origin.x * distance_tangent_point +- r * origin.y * sqrt(distance2 + 2 * r * distance_tangent_point)) / distance2
	//  y = sqrt(2 * r2 - x * x)
	// where r2 - origin.x * x - r * distance_tangent_point == origin.y * y
	//  x = (r2 * origin.x - r * origin.x * distance_tangent_point +- r * origin.y * sqrt(distance2 + 2 * r * distance_tangent_point)) / distance2
	//  y = - sqrt(2 * r2 - x * x)
	// where r2 - origin.x * x - r * distance_tangent_point == - origin.y * y

	minus_b = r2 * origin.x - origin.x * r * distance_tangent_point;
	discriminant = r * origin.y * sqrt(distance2 + 2 * r * distance_tangent_point);

	x = (minus_b - discriminant) / distance2;
	y = ((2 * r2 >= x * x) ? sqrt(2 * r2 - x * x) : 0); // handle the case when the argument is negative due to rounding errors
	temp = r2 - origin.x * x - r * distance_tangent_point; // exclude false roots that appeared when squaring the original equation
	if (fabs(temp - origin.y * y) < FLOAT_ERROR)
		moves_count += pawn_move_set(moves + moves_count, pawn, distance_covered, x, y, origin);
	if (fabs(temp + origin.y * y) < FLOAT_ERROR)
		moves_count += pawn_move_set(moves + moves_count, pawn, distance_covered, x, -y, origin);

	x = (minus_b + discriminant) / distance2;
	y = ((2 * r2 >= x * x) ? sqrt(2 * r2 - x * x) : 0); // handle the case when the argument is negative due to rounding errors
	temp = r2 - origin.x * x - r * distance_tangent_point; // exclude false roots that appeared when squaring the original equation
	if (fabs(temp - origin.y * y) < FLOAT_ERROR)
		moves_count += pawn_move_set(moves + moves_count, pawn, distance_covered, x, y, origin);
	if (fabs(temp + origin.y * y) < FLOAT_ERROR)
		moves_count += pawn_move_set(moves + moves_count, pawn, distance_covered, x, -y, origin);

	assert(moves_count <= 2);
	return moves_count;
}
