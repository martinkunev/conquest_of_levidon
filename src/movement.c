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
#include <stdbool.h>
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

#define PATH_QUEUE_LIMIT 8

struct collision
{
	struct array_pawns pawns;
	bool enemy; // whether the pawn collides with an enemy
	bool slow; // whether the pawn is too slow to continue moving

	unsigned char fastest_speed; // speed of the fastest pawn colliding with the target pawn
	unsigned fastest_count; // number of fastest pawns colliding with the target pawn
};

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

void pawn_place(struct battle *restrict battle, struct pawn *restrict pawn, struct tile tile)
{
	struct battlefield *field;

	pawn->position = (struct position){tile.x + PAWN_RADIUS, tile.y + PAWN_RADIUS};
	pawn->path.count = 0;
	pawn->moves.count = 0;

	field = battle_field(battle, tile);
	field->pawn = pawn;
}

void pawn_stay(struct pawn *restrict pawn)
{
	pawn->path.count = 0;
	pawn->moves.count = 0;
}

static void index_add(struct battle *restrict battle, int x, int y, struct pawn *restrict pawn)
{
	struct battlefield *restrict field;
	size_t i;

	if ((x < 0) || (x >= BATTLEFIELD_WIDTH) || (y < 0) || (y >= BATTLEFIELD_HEIGHT))
		return;

	field = &battle->field[y][x];

	// TODO improve the code below
	for(i = 0; field->pawns[i]; ++i)
	{
		assert(i < sizeof(field->pawns) / sizeof(*field->pawns) - 1);
	}
	field->pawns[i] = pawn;
}

// Build index of pawns by battle field.
void battlefield_index_build(struct battle *restrict battle)
{
	// Clear the index.
	for(size_t y = 0; y < BATTLEFIELD_HEIGHT; ++y)
		for(size_t x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			struct battlefield *field = &battle->field[y][x];
			for(size_t i = 0; i < sizeof(field->pawns) / sizeof(*field->pawns); ++i)
				field->pawns[i] = 0;
		}

	for(size_t i = 0; i < battle->pawns_count; ++i)
	{
		struct position position = battle->pawns[i].position;
		int x = (int)position.x;
		int y = (int)position.y;

		// Add pointer to the pawn in each field that the pawn occupies.

		index_add(battle, x, y, battle->pawns + i);

		if (position.y - y < PAWN_RADIUS)
			index_add(battle, x, y - 1, battle->pawns + i);
		else if (y + 1 - position.y < PAWN_RADIUS)
			index_add(battle, x, y + 1, battle->pawns + i);

		if (position.x - x < PAWN_RADIUS)
			index_add(battle, x - 1, y, battle->pawns + i);
		else if (x + 1 - position.x < PAWN_RADIUS)
			index_add(battle, x + 1, y, battle->pawns + i);

		if (battlefield_distance(position, (struct position){x, y}) < PAWN_RADIUS)
			index_add(battle, x - 1, y - 1, battle->pawns + i);
		else if (battlefield_distance(position, (struct position){x + 1, y + 1}) < PAWN_RADIUS)
			index_add(battle, x + 1, y + 1, battle->pawns + i);

		if (battlefield_distance(position, (struct position){x + 1, y}) < PAWN_RADIUS)
			index_add(battle, x + 1, y - 1, battle->pawns + i);
		else if (battlefield_distance(position, (struct position){x, y + 1}) < PAWN_RADIUS)
			index_add(battle, x - 1, y + 1, battle->pawns + i);
	}
}

// Calculate the position of the pawn in the next round.
struct position movement_position(const struct pawn *restrict pawn)
{
	struct position position = pawn->position;
	size_t move_current = 0;
	double distance_covered = pawn->troop->unit->speed, distance;
	double progress;

	// Determine which is the move in progress and how much of the distance is covered.
	while (1)
	{
		if (move_current == pawn->moves.count)
			return position;

		distance = battlefield_distance(position, pawn->moves.data[move_current]);
		if (distance_covered < distance) // unfinished move
			break;

		distance_covered -= distance;
		position = pawn->moves.data[move_current];
		move_current += 1;
	}

	progress = distance_covered / distance;
	position.x = position.x * (1 - progress) + pawn->moves.data[move_current].x * progress;
	position.y = position.y * (1 - progress) + pawn->moves.data[move_current].y * progress;
	return position;
}

// Calculates the expected position of each pawn at the next step.
int movement_plan(struct battle *restrict battle, struct adjacency_list *restrict graph[static PLAYERS_LIMIT], struct obstacles *restrict obstacles[static PLAYERS_LIMIT])
{
	for(size_t i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *pawn = battle->pawns + i;
		struct position position = pawn->position;
		double distance, distance_covered;
		double progress;

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
			else if ((pawn->action == ACTION_FIGHT) && !can_fight(position, pawn->target.pawn))
				destination = pawn->target.pawn->position;
			else if ((pawn->action == ACTION_ASSAULT) && !can_assault(position, pawn->target.field))
				destination = (struct position){pawn->target.field->tile.x, pawn->target.field->tile.y};
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
			if (pawn->moves.count) memmove(pawn->moves.data, pawn->moves.data + 1, pawn->moves.count * sizeof(*pawn->moves.data));
			else
			{
				// Remove the position just reached from the queue of paths.
				pawn->path.count -= 1;
				if (pawn->path.count) memmove(pawn->path.data, pawn->path.data + 1, pawn->path.count * sizeof(*pawn->path.data));

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

// Detects which pawns collide with the specified pawn and collects statistics about fastest pawns in the collision.
// TODO ?implement a faster algorithm: http://stackoverflow.com/questions/36401562/find-overlapping-circles
static int collisions_detect(const struct game *restrict game, const struct battle *restrict battle, const struct pawn *restrict pawn, struct collision *restrict collision)
{
	collision->fastest_speed = pawn->troop->unit->speed;
	collision->fastest_count = 1;

	for(size_t i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *other = battle->pawns + i;

		if (!other->count)
			continue;
		if (other == pawn)
			continue;

		if (!pawns_collide(pawn->position_next, other->position_next))
			continue;

		if (array_pawns_expand(&collision->pawns, collision->pawns.count + 1) < 0)
			return ERROR_MEMORY;
		collision->pawns.data[collision->pawns.count++] = other;

		if (allies(game, pawn->troop->owner, other->troop->owner))
		{
			if (position_eq(other->position, other->position_next)) // the pawn collides with a stationary pawn
				continue; // collision resolution doesn't affect stationary pawns

			// Update statistics about fastest pawn with the data of the colliding pawn.
			if (other->troop->unit->speed > collision->fastest_speed)
			{
				collision->fastest_speed = other->troop->unit->speed;
				collision->fastest_count = 1;
			}
			else if (other->troop->unit->speed == collision->fastest_speed)
			{
				collision->fastest_count += 1;
			}
		}
		else collision->enemy = true;
	}

	if (collision->fastest_speed > pawn->troop->unit->speed)
		collision->slow = true;

	return 0;
}

static inline double cross_product(double fx, double fy, double sx, double sy)
{
	// TODO this is for right-handed coordinate system
	return (fx * sy - sx * fy);
}

int movement_collisions_resolve(const struct game *restrict game, struct battle *restrict battle, struct adjacency_list *restrict graph[static PLAYERS_LIMIT], struct obstacles *restrict obstacles[static PLAYERS_LIMIT])
{
	struct collision *restrict collisions;
	size_t i;
	int status;

	// Each pawn can forsee what will happen in one movement step if nothing changes and can adjust its movement to react to that.
	// If a pawn forsees a collision with an enemy, it will stay at its current position to fight the surrounding enemies.

	// Pawns that forsee a collision with allies will try to make an alternative movement in order to avoid the collision and continue to their destination. Slower pawns will stop and wait for the faster pawns to pass. If the alternative movement of the faster pawn still leads to a collision, that pawn will stay at its current position.

	collisions = malloc(battle->pawns_count * sizeof(*collisions));
	if (!collisions) return ERROR_MEMORY;
	memset(collisions, 0, battle->pawns_count * sizeof(*collisions));
	for(i = 0; i < battle->pawns_count; ++i)
		collisions[i].pawns = (struct array_pawns){0};

	// Modify next positions of pawns until all positions are certain. Positions that are certain cannot be changed.
	// A position being certain is indicated by pawn->position == pawn->position_next.

	// TODO can I optimize this?

	while (1)
	{
		bool changed = false;

		for(i = 0; i < battle->pawns_count; ++i)
		{
			struct pawn *restrict pawn = battle->pawns + i;

			if (!pawn->count)
				continue; // skip dead pawns

			collisions[i].pawns.count = 0;

			if (position_eq(pawn->position, pawn->position_next))
				continue; // skip non-moving pawns

			status = collisions_detect(game, battle, pawn, collisions + i);
			if (status < 0)
				goto finally;

			if (collisions[i].enemy || collisions[i].slow) changed = true;
		}

		if (!changed) break; // break if there are no more collisions to handle here

		// Each pawn that would collide with an enemy will instead stay at its current position.
		// Each pawn that would collide with a faster ally will instead stay at its current position.
		for(i = 0; i < battle->pawns_count; ++i)
		{
			if (collisions[i].enemy)
			{
				battle->pawns[i].position_next = battle->pawns[i].position;
				collisions[i].enemy = false;
			}

			if (collisions[i].slow)
				battle->pawns[i].position_next = battle->pawns[i].position;
		}
	}

	// Now there are only collisions between moving pawns and (other moving pawns with the same speed or non-moving pawns).

	// Try to resolve pawn collisions by modifying paths of the fastest pawns.
	// If the collision is not easy to resolve, stop the pawn from moving.
	for(i = 0; i < battle->pawns_count; ++i)
	{
		struct pawn *restrict pawn = battle->pawns + i;
		const double distance_covered = (double)pawn->troop->unit->speed / MOVEMENT_STEPS;

		if (!collisions[i].pawns.count)
			continue;

		// TODO support resolving more complicated collisions (pawns should be smarter)

		if ((collisions[i].fastest_count == 1) && (collisions[i].pawns.count == 1))
		{
			struct pawn *obstacle = collisions[i].pawns.data[0];
			struct position moves[2];
			unsigned moves_count;

			// Find the moves in direction of the tangent of the obstacle pawn.
			// Choose one move randomly from the possible moves.
			moves_count = path_moves_tangent(pawn, obstacle, distance_covered, moves);
			if (moves_count)
				pawn->position_next = moves[random() % moves_count];
		}
		else if ((collisions[i].fastest_count == 2) && (collisions[i].pawns.count == 1))
		{
			struct pawn *obstacle = collisions[i].pawns.data[0];
			struct position moves[2];
			unsigned moves_count;

			double direction_x = pawn->position_next.x - pawn->position.x;
			double direction_y = pawn->position_next.y - pawn->position.y;

			// Find the moves in direction of the tangent of the obstacle pawn.
			// Choose the move on the right side of the collision direction.
			moves_count = path_moves_tangent(pawn, obstacle, distance_covered, moves);
			while (moves_count--)
				if (cross_product(moves[moves_count].x - pawn->position.x, moves[moves_count].y - pawn->position.y, direction_x, direction_y) > 0)
				{
					pawn->position_next = moves[moves_count];
					break;
				}
		}
		else
		{
			// There is no easy way to resolve the collision. Make the pawn stay at its current position.
			pawn->position_next = pawn->position;
		}
	}

	// Look for collisions and stop pawns that would collide until all collisions are resolved.
	while (1)
	{
		bool ready = true;

		for(i = 0; i < battle->pawns_count; ++i)
		{
			struct pawn *restrict pawn = battle->pawns + i;

			if (!pawn->count)
				continue; // skip dead pawns
			if (position_eq(pawn->position, pawn->position_next))
				continue; // skip non-moving pawns

			collisions[i].pawns.count = 0;
			status = collisions_detect(game, battle, pawn, collisions + i);
			if (status < 0)
				goto finally;

			if (collisions[i].pawns.count)
				ready = false;
		}

		if (ready) break; // break if there are no more collisions

		// Each pawn that would collide will instead stay at its current position.
		for(i = 0; i < battle->pawns_count; ++i)
			if (collisions[i].pawns.count)
				battle->pawns[i].position_next = battle->pawns[i].position;
	}

	// Update current pawn positions.
	for(i = 0; i < battle->pawns_count; ++i)
		battle->pawns[i].position = battle->pawns[i].position_next;

	status = 0;

finally:
	for(i = 0; i < battle->pawns_count; ++i)
		array_pawns_term(&collisions[i].pawns);
	free(collisions);

	return status;
}

int movement_queue(struct pawn *restrict pawn, struct position target, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	int status;

	if (pawn->path.count == PATH_QUEUE_LIMIT)
		return ERROR_INPUT;

	if (!in_battlefield(target.x, target.y))
		return ERROR_INPUT;

	if (pawn->path.count)
	{
		// Check if the target is reachable.
		double distance = path_distance(pawn, target, graph, obstacles);
		if (isnan(distance))
			return ERROR_MEMORY;
		if (distance == INFINITY)
			return ERROR_MISSING;
	}
	else if (status = path_find(pawn, target, graph, obstacles))
	{
		return status;
	}

	status = array_moves_expand(&pawn->path, pawn->path.count + 1);
	if (status < 0)
		return ERROR_MEMORY;
	pawn->path.data[pawn->path.count++] = target;
	return 0;
}
