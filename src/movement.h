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

// MOVEMENT_STEPS is chosen as to ensure that enemies are close enough to fight on the step before they collide.
#define MOVEMENT_STEPS (unsigned)(2 * UNIT_SPEED_LIMIT * STEPS_FIELD)

struct pawn;

struct array_moves
{
	size_t count;
	size_t capacity;
	struct position *data;
};

static inline void array_moves_term(struct array_moves *restrict array)
{
	free(array->data);
}

int array_moves_expand(struct array_moves *restrict array, size_t count);

struct tile
{
	int x, y; // use signed int to ensure subtraction doesn't wrap around.
};

static inline int position_eq(struct position a, struct position b)
{
	return ((a.x == b.x) && (a.y == b.y));
}

void pawn_place(struct battle *restrict battle, struct pawn *restrict pawn, struct tile tile);
void pawn_stay(struct pawn *restrict pawn);

void battlefield_index_build(struct battle *restrict battle);

struct position movement_position(const struct pawn *restrict pawn);

int movement_plan(struct battle *restrict battle, struct adjacency_list *restrict graph[static PLAYERS_LIMIT], struct obstacles *restrict obstacles[static PLAYERS_LIMIT]);
int movement_collisions_resolve(const struct game *restrict game, struct battle *restrict battle, struct adjacency_list *restrict graph[static PLAYERS_LIMIT], struct obstacles *restrict obstacles[static PLAYERS_LIMIT]);

int movement_queue(struct pawn *restrict pawn, struct position target, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles);
