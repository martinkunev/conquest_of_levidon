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

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "errors.h"
#include "game.h"
#include "battle.h"
#include "pathfinding.h"

#define PAWN_RADIUS 0.5
#define WALL_THICKNESS 0.5
#define WALL_OFFSET ((1 - WALL_THICKNESS) / 2)

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

// The obstacle doesn't block paths right next to it ("touching" the obstacle is allowed).
static int path_blocked_obstacle(struct position start, struct position end, const struct obstacle *restrict obstacle)
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
		if ((start.y >= obstacle->top) && (end.y >= obstacle->top)) return 0;
		if ((start.y <= obstacle->bottom) && (end.y <= obstacle->bottom)) return 0;

		return 1;
	}

	return 0;
}

// WARNING: The path must be a non-zero vector (end != start).
static int path_blocked_pawn(struct position start, struct position end, struct position pawn)
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
	int horizontal = -1; // start coordinate of the last detected horizontal wall
	int vertical[BATTLEFIELD_WIDTH]; // start coordinate of the last detected vertical walls
	for(i = 0; i < BATTLEFIELD_WIDTH; ++i)
		vertical[i] = -1;

	// Count the obstacles.
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
	{
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			const struct battlefield *field = &battle->field[y][x];
			if (!battlefield_passable(game, field, player))
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
						horizontal = x;
					else if (field->position & POSITION_LEFT)
						obstacles_count += 1;
				}
				if (vertical[x] >= 0)
				{
					if (field->position != (field->position | POSITION_TOP | POSITION_BOTTOM))
						obstacles_count += 1;
					if (!(field->position & POSITION_TOP)) vertical[x] = -1;
				}
				else
				{
					if (field->position & POSITION_TOP)
						vertical[x] = y;
					else if (field->position & POSITION_BOTTOM)
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
			if (!battlefield_passable(game, field, player))
			{
				if (horizontal >= 0)
				{
					if (field->position != (field->position | POSITION_LEFT | POSITION_RIGHT))
						obstacle_insert(obstacles, horizontal, y + WALL_OFFSET, x + (1 - WALL_OFFSET) * ((field->position & POSITION_LEFT) != 0), y + (1 - WALL_OFFSET));
					if (!(field->position & POSITION_RIGHT)) horizontal = -1;
				}
				else
				{
					if (field->position & POSITION_RIGHT)
						horizontal = x + WALL_OFFSET * !(field->position & POSITION_LEFT);
					else if (field->position & POSITION_LEFT)
						obstacle_insert(obstacles, x, y + WALL_OFFSET, x + (1 - WALL_OFFSET), y + (1 - WALL_OFFSET));
				}
				if (vertical[x] >= 0)
				{
					if (field->position != (field->position | POSITION_TOP | POSITION_BOTTOM))
						obstacle_insert(obstacles, x + WALL_OFFSET, vertical[x], x + (1 - WALL_OFFSET), y + (1 - WALL_OFFSET) * ((field->position & POSITION_BOTTOM) != 0));
					if (!(field->position & POSITION_TOP)) vertical[x] = -1;
				}
				else
				{
					if (field->position & POSITION_TOP)
						vertical[x] = y + WALL_OFFSET * !(field->position & POSITION_BOTTOM);
					else if (field->position & POSITION_BOTTOM)
						obstacle_insert(obstacles, x + WALL_OFFSET, y, x + (1 - WALL_OFFSET), y + (1 - WALL_OFFSET));
				}
			}
			else
			{
				if (horizontal >= 0)
				{
					obstacle_insert(obstacles, horizontal, y + WALL_OFFSET, x, y + (1 - WALL_OFFSET));
					horizontal = -1;
				}
				if (vertical[x] >= 0)
				{
					obstacle_insert(obstacles, x + WALL_OFFSET, vertical[x], x + (1 - WALL_OFFSET), y);
					vertical[x] = -1;
				}
			}
		}

		if (horizontal >= 0)
		{
			obstacle_insert(obstacles, horizontal, y + WALL_OFFSET, BATTLEFIELD_WIDTH, y + (1 - WALL_OFFSET));
			horizontal = -1;
		}
	}
	for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
	{
		if (vertical[x] >= 0)
		{
			obstacle_insert(obstacles, x + WALL_OFFSET, vertical[x], x + (1 - WALL_OFFSET), BATTLEFIELD_HEIGHT);
			vertical[x] = -1;
		}
	}

	return obstacles;
}

// Checks whether a pawn can see and move directly from origin to target (there are no obstacles in-between).
int path_visible(struct position origin, struct position target, const struct obstacles *restrict obstacles)
{
	size_t i;

	// Check if there is an obstacle that blocks the path from origin to target.
	for(i = 0; i < obstacles->count; ++i)
		if (path_blocked_obstacle(origin, target, obstacles->obstacle + i))
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

static void graph_vertex_insert(struct adjacency_list *graph, float x, float y, unsigned char occupied[static BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
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
struct adjacency_list *visibility_graph_build(const struct battle *restrict battle, const struct obstacles *restrict obstacles)
{
	size_t i, j;

	struct adjacency_list *graph, *graph_resized;

	unsigned char occupied[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH] = {0};
	for(i = 0; i < BATTLEFIELD_HEIGHT; ++i)
		for(j = 0; j < BATTLEFIELD_WIDTH; ++j)
			if (battle->field[i][j].blockage)
				occupied[i][j] = 1;

	// Allocate enough memory for the maximum size of the adjacency graph.
	// 4 vertices for the corners around each obstacle and 2 vertices for origin and target
	graph = malloc(offsetof(struct adjacency_list, list) + sizeof(*graph->list) * (obstacles->count * 4 + 2));
	if (!graph) return 0;
	graph->count = 0;

	for(i = 0; i < obstacles->count; ++i)
	{
		const struct obstacle *restrict obstacle = obstacles->obstacle + i;
		graph_vertex_insert(graph, obstacle->left - PAWN_RADIUS, obstacle->bottom - PAWN_RADIUS, occupied);
		graph_vertex_insert(graph, obstacle->right + PAWN_RADIUS, obstacle->bottom - PAWN_RADIUS, occupied);
		graph_vertex_insert(graph, obstacle->right + PAWN_RADIUS, obstacle->top + PAWN_RADIUS, occupied);
		graph_vertex_insert(graph, obstacle->left - PAWN_RADIUS, obstacle->top + PAWN_RADIUS, occupied);
	}

	// Free the unused part of the adjacency graph buffer.
	// Leave space for origin and target vertices.
	graph_resized = realloc(graph, sizeof(*graph) + sizeof(*graph->list) * (graph->count + 2));
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
	size_t i;
	struct neighbor *item, *next;

	if (!graph) return;

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
