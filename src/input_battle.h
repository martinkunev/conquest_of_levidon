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

struct state_battle
{
	unsigned char player; // current player

	struct point hover; // position of the hovered field

	const struct obstacles *obstacles; // obstacles on the battlefield
	struct adjacency_list *graph; // graph used for pathfinding

	struct point field; // selected field
	struct pawn *pawn; // selected pawn

	double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];
};

struct state_formation
{
	unsigned char player; // current player

	struct point hover; // position of the hovered field

	struct pawn *pawn; // selected pawn

	struct point reachable[REACHABLE_LIMIT];
	size_t reachable_count;
};

struct state_animation
{
	struct battle *battle;

	struct timeval start; // start time of the animation

	unsigned char traversed[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];
};

int input_formation(const struct game *restrict game, struct battle *restrict battle, unsigned char player);
int input_battle(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles);
int input_animation(const struct game *restrict game, const struct battle *restrict battle, struct position (*movements)[MOVEMENT_STEPS + 1]);
