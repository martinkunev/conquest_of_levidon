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

#define REPORT_TITLE_NEXT "Next player"
#define REPORT_TITLE_DEFEATED "Player defeated"
#define REPORT_TITLE_BATTLE "Battle"

struct state_report
{
	const struct game *game;
	const struct battle *battle;

	const char *title;
	size_t title_size;

	size_t players_count;
	unsigned char players[PLAYERS_LIMIT];
};

struct state_question
{
	const struct game *game;
	unsigned char player;

	struct region *region;
};

int input_report_battle(const struct game *restrict game, const struct battle *restrict battle);
int input_report_map(const struct game *restrict game);
int input_report_players(struct state_report *state);
int input_report_invasion(const struct game *restrict game, unsigned char player, struct region *restrict region);
