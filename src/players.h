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

enum {REQUEST_MAP = 1, REQUEST_INVASION, REQUEST_FORMATION, REQUEST_BATTLE};

struct request_map
{
	const struct game *game;
};

struct request_invasion
{
	const struct game *game;
	const struct region *region;
};

struct request_formation
{
	const struct game *game;
	const struct battle *battle;
};

struct request_battle
{
	const struct game *game;
	const struct battle *battle;
	struct adjacency_list *restrict graph;
	const struct obstacles *restrict obstacles;
};

int player_init(struct player *restrict player, unsigned char id);
int player_term(struct player *restrict player);

void player_map(const struct game *restrict game, unsigned char player);
void player_invasion(const struct game *restrict game, const struct region *restrict region, unsigned char player);
void player_formation(const struct game *restrict game, const struct battle *restrict battle, unsigned char player);
void player_battle(const struct game *restrict game, const struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles);

int player_response(const struct game *restrict game, unsigned char player);
