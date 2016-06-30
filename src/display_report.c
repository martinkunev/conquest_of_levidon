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

#include <GL/glx.h>

#include <stdlib.h>

#include "format.h"
#include "game.h"
#include "draw.h"
#include "map.h"
#include "interface.h"
#include "input_report.h"
#include "display_common.h"
#include "display_report.h"
#include "pathfinding.h"
#include "movement.h"
#include "battle.h"
#include "display_battle.h"

#define FORMAT_BUFFER_INT (1 + sizeof(uintmax_t) * 3) /* buffer size that is sufficient for base-10 representation of any integer */

#define S(s) (s), sizeof(s) - 1

#define REPORT_BEFORE_X 32
#define REPORT_AFTER_X 416

#define REPORT_PLAYER_X 32
#define REPORT_REGIONS_X 96
#define REPORT_GOLD_X 160
#define REPORT_FOOD_X 224
#define REPORT_WOOD_X 288
#define REPORT_IRON_X 352
#define REPORT_STONE_X 416

#define MARGIN_X 40
#define MARGIN_Y 60

#define TITLE_SIZE_LIMIT 64

#define REPORT_MAP_TITLE S("Winners")

void if_report_battle(const void *argument, const struct game *game_)
{
	const struct state_report *restrict state = argument;
	const struct game *restrict game = state->game;
	const struct battle *restrict battle = state->battle;

	size_t player;
	size_t i;

	unsigned offset[PLAYERS_LIMIT] = {0};
	unsigned position_before[PLAYERS_LIMIT] = {0};
	unsigned position_after[PLAYERS_LIMIT] = {0};
	unsigned offset_next = REPORT_Y;

	// TODO indicate whether the attacker or the defender wins and which players win/lose/surrender
	// TODO somehow indicate which players are allies
	// TODO for assault display the damage to the garrison
	// TODO indicate if the battle ended due to timeout

	// Display report title.
	char title[TITLE_SIZE_LIMIT], *end = title;
	struct box box;
	if (battle->assault) end = format_bytes(end, S("Assault in "));
	else end = format_bytes(end, S("Open battle in "));
	end = format_bytes(end, battle->region->name, battle->region->name_length);
	box = string_box(title, end - title, &font24);
	draw_string(title, end - title, (SCREEN_WIDTH - box.width) / 2, TITLE_Y, &font24, White);

	draw_string(S("Before the battle"), REPORT_BEFORE_X, LABEL_Y, &font12, White);
	draw_string(S("After the battle"), REPORT_AFTER_X, LABEL_Y, &font12, White);

	for(player = 0; player < game->players_count; ++player)
	{
		for(i = 0; i < battle->players[player].pawns_count; ++i)
		{
			const struct pawn *restrict pawn = battle->players[player].pawns[i];
			unsigned char owner = pawn->troop->owner;

			if (!offset[owner])
			{
				offset[owner] = offset_next;
				offset_next += MARGIN_Y;

				position_before[owner] = REPORT_BEFORE_X;
				position_after[owner] = REPORT_AFTER_X;
			}

			display_troop(pawn->troop->unit->index, position_before[owner], offset[owner], Player + owner, White, pawn->troop->count);
			position_before[owner] += MARGIN_X;

			if (pawn->count)
			{
				display_troop(pawn->troop->unit->index, position_after[owner], offset[owner], Player + owner, White, pawn->count);
				position_after[owner] += MARGIN_X;
			}
		}
	}

	show_button(S("Close"), BUTTON_EXIT_X, BUTTON_EXIT_Y);
}

void if_report_map(const void *argument, const struct game *game)
{
	struct box box;
	size_t player;
	unsigned offset = 0;

	unsigned regions_owned[PLAYERS_LIMIT] = {0};

	for(player = 0; player < game->regions_count; ++player)
		regions_owned[game->regions[player].owner] += 1;

	// Display report title.
	box = string_box(REPORT_MAP_TITLE, &font24);
	draw_string(REPORT_MAP_TITLE, (SCREEN_WIDTH - box.width) / 2, TITLE_Y, &font24, White);
	// TODO display turn number (year, month)

	// Display table headings.
	draw_string(S("player"), REPORT_PLAYER_X, LABEL_Y, &font12, White);
	draw_string(S("regions"), REPORT_REGIONS_X, LABEL_Y, &font12, White);
	draw_string(S("gold"), REPORT_GOLD_X, LABEL_Y, &font12, White);
	draw_string(S("food"), REPORT_FOOD_X, LABEL_Y, &font12, White);
	draw_string(S("wood"), REPORT_WOOD_X, LABEL_Y, &font12, White);
	draw_string(S("iron"), REPORT_IRON_X, LABEL_Y, &font12, White);
	draw_string(S("stone"), REPORT_STONE_X, LABEL_Y, &font12, White);

	for(player = 0; player < game->players_count; ++player)
	{
		unsigned char buffer[FORMAT_BUFFER_INT], *end;

		if (player == PLAYER_NEUTRAL) continue;
		if (!regions_owned[player]) continue;

		offset += MARGIN_Y;

		show_flag(REPORT_PLAYER_X, LABEL_Y + offset, player);

		end = format_uint(buffer, regions_owned[player], 10);
		draw_string(buffer, end - buffer, REPORT_REGIONS_X, LABEL_Y + offset, &font12, White);

		// Display player resources.
		end = format_uint(buffer, game->players[player].treasury.gold, 10);
		draw_string(buffer, end - buffer, REPORT_GOLD_X, LABEL_Y + offset, &font12, White);
		end = format_uint(buffer, game->players[player].treasury.food, 10);
		draw_string(buffer, end - buffer, REPORT_FOOD_X, LABEL_Y + offset, &font12, White);
		end = format_uint(buffer, game->players[player].treasury.wood, 10);
		draw_string(buffer, end - buffer, REPORT_WOOD_X, LABEL_Y + offset, &font12, White);
		end = format_uint(buffer, game->players[player].treasury.iron, 10);
		draw_string(buffer, end - buffer, REPORT_IRON_X, LABEL_Y + offset, &font12, White);
		end = format_uint(buffer, game->players[player].treasury.stone, 10);
		draw_string(buffer, end - buffer, REPORT_STONE_X, LABEL_Y + offset, &font12, White);
	}

	show_button(S("Close"), BUTTON_EXIT_X, BUTTON_EXIT_Y);
}
