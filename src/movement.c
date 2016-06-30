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
#include <string.h>

#include "errors.h"
#include "game.h"
#include "pathfinding.h"
#include "movement.h"
#include "battle.h"
#include "combat.h"
#include "draw.h"
#include "map.h"

#define array_name array_pawns
#define array_type struct pawn *
#include "generic/array.g"

// Due to pawn dimensions, at most 4 pawns can try to move to a given square at the same time.
#define OVERLAP_LIMIT 4

struct collision
{
	struct array_pawns pawns;
	int movement_stop;
	int set;
};

// TODO detect whether a pawn is on a battle field (used for closing the gates)

int array_moves_expand(struct array_moves *restrict array, size_t count)
{
	if (array->capacity < count)
	{
		size_t capacity;
		struct position *buffer;

		// Round count up to the next power of 2 that is >= ARRAY_SIZE_BASE.
		capacity = (array->capacity * 2) | (!array->capacity * 8);
		while (capacity < count)
			capacity *= 2;

		buffer = realloc(array->data, capacity * sizeof(*array->data));
		if (!buffer)
			return -1;
		array->data = buffer;
		array->capacity = capacity;
	}
	return 0;
}

void pawn_place(struct battle *restrict battle, struct pawn *restrict pawn, struct position position)
{
	struct battlefield *field;

	pawn->position = position;
	pawn->path.count = 0;
	pawn->moves.count = 0;

	field = battle_field(battle, pawn->position);
	field->pawn = pawn;
}

static void index_add(struct battlefield *restrict field, struct pawn *restrict pawn)
{
	size_t i;
	for(i = 0; field->pawns[i]; ++i)
	{
		// assert(i < sizeof(field->pawns) / sizeof(*field->pawns) - 1);
	}
	field->pawns[i] = pawn;
}

// Build index of pawns by battle field.
void battlefield_index_build(struct battle *restrict battle)
{
	size_t x, y;
	size_t i;

	// Clear the index.
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			struct battlefield *field = &battle->field[y][x];
			for(i = 0; i < sizeof(field->pawns) / sizeof(*field->pawns); ++i)
				field->pawns[i] = 0;
		}

	for(i = 0; i < battle->pawns_count; ++i)
	{
		struct position position = battle->pawns[i].position;

		x = (unsigned)position.x;
		y = (unsigned)position.y;

		// Add pointer to the pawn in each field that the pawn occupies.

		index_add(&battle->field[y][x], battle->pawns + i);

		if (position.y - y < PAWN_RADIUS)
			index_add(&battle->field[y - 1][x], battle->pawns + i);
		else if (y + 1 - position.y < PAWN_RADIUS)
			index_add(&battle->field[y + 1][x], battle->pawns + i);

		if (position.x - x < PAWN_RADIUS)
			index_add(&battle->field[y][x - 1], battle->pawns + i);
		else if (x + 1 - position.x < PAWN_RADIUS)
			index_add(&battle->field[y][x + 1], battle->pawns + i);

		if (battlefield_distance(position, (struct position){x, y}) < PAWN_RADIUS)
			index_add(&battle->field[y - 1][x - 1], battle->pawns + i);
		else if (battlefield_distance(position, (struct position){x + 1, y + 1}) < PAWN_RADIUS)
			index_add(&battle->field[y + 1][x + 1], battle->pawns + i);

		if (battlefield_distance(position, (struct position){x + 1, y}) < PAWN_RADIUS)
			index_add(&battle->field[y - 1][x + 1], battle->pawns + i);
		else if (battlefield_distance(position, (struct position){x, y + 1}) < PAWN_RADIUS)
			index_add(&battle->field[y + 1][x - 1], battle->pawns + i);
	}
}

static inline int position_diff(struct position a, struct position b)
{
	return ((a.x != b.x) || (a.y != b.y));
}

// Calculates the expected position of each pawn at the next step.
int movement_plan(struct battle *restrict battle, struct adjacency_list *restrict graph[static PLAYERS_LIMIT], struct obstacles *restrict obstacles[static PLAYERS_LIMIT])
{
	size_t i;

	for(i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *pawn = battle->pawns + i;
		struct position position = pawn->position;
		double distance, distance_covered;
		double progress;

		// assert(pawn->position == pawn->position_next);

		distance_covered = (double)pawn->troop->unit->speed / MOVEMENT_STEPS;

		// Delete moves if they represent fighting target (the target may have moved).
		// TODO optimization: only do this if the target moved
		if (!pawn->path.count && (pawn->action == ACTION_FIGHT))
			pawn->moves.count = 0;

		// Make sure there are precalculated moves for the pawn.
		if (!pawn->moves.count)
		{
			struct position destination;

path_find_next:
			// Determine the next destination.
			if (pawn->path.count)
				destination = pawn->path.data[0];
			else if ((pawn->action == ACTION_FIGHT) && position_diff(position, pawn->target.pawn->position))
				destination = pawn->target.pawn->position;
			else
			{
				pawn->position_next = position;
				continue; // nothing to do for this pawn
			}

			switch (path_find(pawn, destination, graph[pawn->troop->owner], obstacles[pawn->troop->owner]))
			{
			case ERROR_MEMORY:
				return ERROR_MEMORY;

			case ERROR_MISSING:
				pawn->position_next = position;
				continue; // cannot reach destination at this time
			}
		}

		// Determine which is the move in progress and how much of the distance is covered.
		while (1)
		{
			distance = battlefield_distance(position, pawn->moves.data[0]);
			if (distance_covered < distance)
				break;

			distance_covered -= distance;
			position = pawn->moves.data[0];

			// Remove the position just reached from the queue of moves.
			pawn->moves.count -= 1;
			if (pawn->moves.count) memmove(pawn->moves.data, pawn->moves.data + 1, pawn->moves.count);
			else
			{
				// Remove the position just reached from the queue of paths.
				pawn->path.count -= 1;
				if (pawn->path.count) memmove(pawn->path.data, pawn->path.data + 1, pawn->path.count);

				goto path_find_next;
			}
		}

		// Calculate the next position of the pawn.
		progress = distance_covered / distance;
		pawn->position_next.x = position.x * (1 - progress) + pawn->moves.data[0].x * progress;
		pawn->position_next.y = position.y * (1 - progress) + pawn->moves.data[0].y * progress;
	}

	return 0;
}

// Find pawn movement collisions. Returns whether there are collisions between enemies.
// TODO the name of the function doesn't correspond to its return value
// TODO implement a faster algorithm: http://stackoverflow.com/questions/36401562/find-overlapping-circles
static int collisions_detect(const struct game *restrict game, const struct battle *restrict battle, struct collision *restrict collisions)
{
	size_t i, j;

	int enemies_colliding = 0;

	for(i = 0; i < battle->pawns_count; ++i)
	{
		if (collisions[i].set)
			continue;
		collisions[i].set = 1;

		for(j = 0; j < battle->pawns_count; ++j)
		{
			if (i == j)
				continue;

			if (pawns_collide(battle->pawns[i].position, battle->pawns[j].position))
			{
				if (array_pawns_expand(&collisions[i].pawns, collisions[i].pawns.count + 1) < 0)
					return ERROR_MEMORY;
				collisions[i].pawns.data[collisions[i].pawns.count] = battle->pawns + j;
				collisions[i].pawns.count += 1;

				// Mark for stopping a pawn that collides with an enemy.
				if (!allies(game, battle->pawns[i].troop->owner, battle->pawns[j].troop->owner))
				{
					enemies_colliding = 1;
					collisions[i].movement_stop = 1;
				}
			}
		}
	}

	return enemies_colliding;
}

// Find the moving colliding pawns that are faster than the moving pawns they are colliding with.
static int collisions_fastest(struct battle *restrict battle, const struct collision *restrict collisions, struct array_pawns *restrict fastest)
{
	size_t i, j;

	*fastest = (struct array_pawns){0};

	for(i = 0; i < battle->pawns_count; ++i)
	{
		unsigned speed = battle->pawns[i].troop->unit->speed;

		if (position_eq(battle->pawns[i].position, battle->pawns[i].position_next) || !collisions[i].pawns.count)
			continue;

		for(j = 0; j < collisions[i].pawns.count; ++j)
		{
			const struct pawn *pawn_colliding = collisions[i].pawns.data[j];

			if (position_eq(pawn_colliding->position, pawn_colliding->position_next)) // the pawn collides with a stationary pawn
				goto skip; // TODO add stationary pawn as an obstacle; add graph vertices
			if (pawn_colliding->troop->unit->speed >= speed) // the pawn collides with a faster pawn
				goto skip;
		}
		if (array_pawns_expand(fastest, fastest->count + 1) < 0)
			return ERROR_MEMORY;
		fastest->data[fastest->count] = battle->pawns + i;
		fastest->count += 1;
		continue;
skip:
		// For the moment make the colliding pawns stay at their current location. TODO remove this line and handle this in the calling function
		battle->pawns[i].position_next = battle->pawns[i].position;
	}

	return 0;
}

int movement_collisions_resolve(const struct game *restrict game, struct battle *restrict battle, struct adjacency_list *restrict graph[static PLAYERS_LIMIT], struct obstacles *restrict obstacles[static PLAYERS_LIMIT])
{
	struct collision *restrict collisions;
	struct array_pawns fastest;

	size_t i;

	int status;

	// Each pawn can forsee what will happen in one movement step if nothing changes and can adjust its movement to react to that.
	// If a pawn forsees a collision with an enemy, it will stay at its current position to fight the surrounding enemies.
	// Pawns that forsee a collision with allies will try to make an alternative movement in order to avoid the collision and continue to their destination. Faster pawns are given higher priority. If the alternative movement still leads a pawn to a collision, that pawn will stay at its current position.

	collisions = malloc(battle->pawns_count * sizeof(*collisions));
	if (!collisions) return ERROR_MEMORY;
	memset(collisions, 0, battle->pawns_count * sizeof(*collisions));
	for(i = 0; i < battle->pawns_count; ++i)
		collisions[i].pawns.data = 0;

	// Modify next positions of pawns until all positions are certain. Positions that are certain cannot be changed.
	// A position being certain is indicated by pawn->position == pawn->position_next.

	while (collisions_detect(game, battle, collisions))
	{
		if (status < 0)
		{
			free(collisions);
			return status;
		}

		// Each pawn that would collide with an enemy will instead stay at its current position.
		for(i = 0; i < battle->pawns_count; ++i)
		{
			struct pawn *pawn = battle->pawns + i;

			if (collisions[i].movement_stop)
			{
				collisions[i].set = 0;
				collisions[i].pawns.count = 0;
				collisions[i].movement_stop = 0;

				pawn->position_next = pawn->position;
			}
		}
	}

	// Now all colliding pawns are allies.

	// TODO at this point I could know whether there are allies colliding (and optimize based on this knowledge)

	// Each pawn that would collide with a non-moving ally should change its movement path to avoid the collision.
	if (status = collisions_fastest(battle, collisions, &fastest))
	{
		array_pawns_term(&fastest);
		free(collisions);
		return status;
	}

	// TODO ? with the current logic, each colliding pawn needs to have a separate list of obstacles to which positions and paths of other pawns are added

	/* TODO implement modification of paths; some idea of the logic to do this is:
	while there are pawns colliding:
		while there are pawns, each one of which overlaps only with slower pawns:
			keep the path of the fastest pawns; modify the paths of the slower pawns while considering the faster pawns as obstacles

		take each pawn that collides with a pawn with the same speed and no pawns with higher speed
			choose a path for the pawn
		for all pawns just taken that still collide with a pawn with the same speed
			make it stay at its current position
	*/

	array_pawns_term(&fastest);

	free(collisions);

	// Update current pawn positions.
	for(i = 0; i < battle->pawns_count; ++i)
		battle->pawns[i].position = battle->pawns[i].position_next;

	return 0;
}

void movement_stay(struct pawn *restrict pawn)
{
	pawn->path.count = 0;
	pawn->moves.count = 0;
}

int movement_queue(struct pawn *restrict pawn, struct position target, struct adjacency_list *restrict nodes, const struct obstacles *restrict obstacles)
{
	// TODO verify that the target is reachable

	int status = array_moves_expand(&pawn->path, pawn->path.count + 1);
	if (status < 0) return ERROR_MEMORY;
	pawn->path.data[pawn->path.count++] = target;
	return 0;
}

/*
unsigned movement_visited(const struct pawn *restrict pawn, struct point visited[static UNIT_SPEED_LIMIT])
{
	struct point location;
	unsigned fields_count;

	size_t move_index;
	unsigned step;

	// Add the start location to the visited fields.
	visited[0] = pawn->moves[0].location;
	fields_count = 1;

	move_index = 0;
	for(step = 1; step <= UNIT_SPEED_LIMIT; ++step)
	{
		double time_now = (double)step / UNIT_SPEED_LIMIT, time_start;
		double progress; // progress of the current move; 0 == start point; 1 == end point

		if (time_now < pawn->moves[0].time) continue; // no movement yet

		while (time_now >= pawn->moves[move_index].time)
		{
			move_index += 1;
			if (move_index == pawn->moves_count) goto finally;
		}

		time_start = pawn->moves[move_index - 1].time;
		progress = (time_now - time_start) / (pawn->moves[move_index].time - time_start);
		location.x = pawn->moves[move_index].location.x * progress + pawn->moves[move_index - 1].location.x * (1 - progress) + 0.5;
		location.y = pawn->moves[move_index].location.y * progress + pawn->moves[move_index - 1].location.y * (1 - progress) + 0.5;

		if (!point_eq(location, visited[fields_count - 1]))
			visited[fields_count++] = location;
	}

finally:
	// Add the final location to the visited fields.
	location = pawn->moves[pawn->moves_count - 1].location;
	if (!point_eq(location, visited[fields_count - 1]))
		visited[fields_count++] = location;

	return fields_count;
}

// WARNING: This function cancels the action of the pawn.
static int movement_follow(struct pawn *restrict pawn, const struct pawn *restrict target, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	size_t i;

	int status;

	status = movement_queue(pawn, target->moves[0].location, graph, obstacles);
	if (status < 0) return status;

	for(i = 1; i < target->moves_count; ++i)
	{
		double time_start, time_end;
		double target_time_duration;

		time_start = pawn->moves[pawn->moves_count - 1].time;
		if (time_start >= 1.0) break; // no need to add moves that won't be started this round

		target_time_duration = target->moves[i].time - target->moves[i - 1].time;

		status = movement_queue(pawn, target->moves[i].location, graph, obstacles);
		if (status < 0)
		{
			if (status == ERROR_MISSING)
			{
				// The pawn can't reach its target.
				// Follow the target to the last field possible.

				double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];

				struct point visited[UNIT_SPEED_LIMIT];
				unsigned visited_count = movement_visited(pawn, visited);

				status = path_distances(pawn, graph, obstacles, reachable);
				if (status < 0) return status;

				while (visited_count--)
				{
					struct point field = visited[visited_count];
					if (reachable[field.y][field.x] < INFINITY)
					{
						status = movement_queue(pawn, target->moves[i].location, graph, obstacles);
						if (status < 0) return status;
						break;
					}
				}

				status = ERROR_MISSING;
			}
			return status;
		}

		time_end = pawn->moves[pawn->moves_count - 1].time;
		if (time_end - time_start < target_time_duration)
		{
			pawn->moves[pawn->moves_count - 1].time = time_start + target_time_duration;
			time_end = pawn->moves[pawn->moves_count - 1].time;
		}
	}

	return 0;
}

// Moves the pawn to the closest position for attacking the specified target.
// ERROR_MEMORY
// ERROR_MISSING - there is no attacking position available
int movement_attack(struct pawn *restrict pawn, struct point target, const struct battlefield field[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH], struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	if (!battlefield_neighbors(pawn->moves[pawn->moves_count - 1].location, target))
	{
		double move_distance = INFINITY;
		int move_x, move_y;

		int x = target.x, y = target.y;

		if ((x > 0) && !field[y][x - 1].pawn && (reachable[y][x - 1] < move_distance))
		{
			move_x = x - 1;
			move_y = y;
			move_distance = reachable[move_y][move_x];
		}
		if ((x < (BATTLEFIELD_WIDTH - 1)) && !field[y][x + 1].pawn && (reachable[y][x + 1] < move_distance))
		{
			move_x = x + 1;
			move_y = y;
			move_distance = reachable[move_y][move_x];
		}
		if ((y > 0) && !field[y - 1][x].pawn && (reachable[y - 1][x] < move_distance))
		{
			move_x = x;
			move_y = y - 1;
			move_distance = reachable[move_y][move_x];
		}
		if ((y < (BATTLEFIELD_HEIGHT - 1)) && !field[y + 1][x].pawn && (reachable[y + 1][x] < move_distance))
		{
			move_x = x;
			move_y = y + 1;
			move_distance = reachable[move_y][move_x];
		}

		if (move_distance < INFINITY)
			return movement_queue(pawn, (struct point){move_x, move_y}, graph, obstacles);
		else
			return ERROR_MISSING;
	}

	return 0;
}

// Plan the necessary movements for a pawn to attack its target.
int movement_attack_plan(struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	int status;

	// Mark all movements as manually assigned.
	pawn->moves_manual = pawn->moves_count;

	if (pawn->action != PAWN_FIGHT) return 0;

	// Nothing to do if the target pawn is immobile.
	if (pawn->target.pawn->moves_count == 1) return 0;

	status = movement_follow(pawn, pawn->target.pawn, graph, obstacles);
	switch (status)
	{
	case ERROR_MISSING: // target pawn is not reachable
		return 0;

	default:
		pawn->action = PAWN_FIGHT;
	case ERROR_MEMORY:
		return status;
	}
}

// Returns the step at which the pawn will first change its position or -1 if it will not move until the end of the round.
static int pawn_move_step(struct pawn *pawns[OVERLAP_LIMIT], size_t pawn_index, unsigned step)
{
	size_t i;
	unsigned step_next;
	for(step_next = step + 1; step_next <= MOVEMENT_STEPS; ++step_next)
	{
		struct point next;
		double time_next = (double)step_next / MOVEMENT_STEPS;
		pawn_position(pawns[pawn_index], time_next, &next);

		if (!point_eq(next, pawns[pawn_index]->step))
		{
			for(i = 0; (i < OVERLAP_LIMIT) && pawns[i]; ++i)
			{
				if (i == pawn_index) continue;

				// Check if the pawn will collide when it moves.
				struct point wait = pawns[i]->failback;
				if ((abs((int)next.x - (int)wait.x) < 2) && (abs((int)next.y - (int)wait.y) < 2))
					return -1;
			}

			return step_next;
		}
	}
	return -1;
}

// Update pawn action and movement.
int battlefield_movement_attack(struct battle *restrict battle, struct pawn *restrict pawn, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	// If the pawn follows another pawn, update its movement to correspond to the current battlefield situation.
	if (pawn->action == PAWN_FIGHT)
	{
		int status;
		double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];

		status = path_distances(pawn, graph, obstacles, reachable);
		if (status < 0) return status;

		status = movement_attack(pawn, pawn->target.pawn->moves[0].location, ((const struct battle *)battle)->field, reachable, graph, obstacles);
		switch (status)
		{
		case ERROR_MISSING: // target pawn is not reachable
			movement_stay(pawn);
			return 0;

		default:
			pawn->action = PAWN_FIGHT;
		case ERROR_MEMORY:
			return status;
		}
	}

	return 0;
}
*/
