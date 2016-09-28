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

int players_init(struct game *restrict game);
int players_term(struct game *restrict game);

int players_map(struct game *restrict game);
int players_invasion(struct game *restrict game, struct region *restrict region);
int players_formation(struct game *restrict game, struct battle *restrict battle, int hotseat);
int players_battle(struct game *restrict game, struct battle *restrict battle, const struct obstacles *restrict obstacles[static PLAYERS_LIMIT], struct adjacency_list *restrict graph[static PLAYERS_LIMIT]);
