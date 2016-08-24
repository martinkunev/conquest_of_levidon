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

struct state_report
{
	const struct game *game;
	const struct battle *battle;

	size_t players_count;
	unsigned char players[PLAYERS_LIMIT];

	unsigned char player;
};

int input_report_battle(const struct game *restrict game, const struct battle *restrict battle);
int input_report_map(const struct game *restrict game);

int input_prepare_player(const struct game *restrict game, unsigned char player);
int input_prepare_battle(struct state_report *restrict state);
