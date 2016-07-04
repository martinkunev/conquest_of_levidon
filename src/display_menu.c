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

#include "base.h"
#include "game.h"
#include "draw.h"
#include "map.h"
#include "interface.h"
#include "input_menu.h"
#include "pathfinding.h"
#include "display_common.h"
#include "display_battle.h"
#include "display_menu.h"
#include "menu.h"
#include "world.h"

#define TITLE "Conquest of Levidon"
#define TITLE_SAVE "Save game"

// TODO display long filenames properly

#define S(s) (s), sizeof(s) - 1

void if_load(const void *argument, const struct game *game)
{
	const struct state *state = argument;

	size_t i;

	struct point position;

	struct box box = string_box(S(TITLE), &font24);
	draw_string(S(TITLE), (SCREEN_WIDTH - box.width) / 2, TITLE_Y, &font24, White);

	// TODO ? separate functions for loaded and !loaded

	// Display world directories tabs.
	if (!state->loaded)
	{
		position = if_position(WorldTabs, 0);
		draw_string(S("system"), position.x + TAB_PADDING, position.y + TAB_PADDING, &font12, White);

		position = if_position(WorldTabs, 1);
		draw_string(S("user"), position.x + TAB_PADDING, position.y + TAB_PADDING, &font12, White);

		position = if_position(WorldTabs, 2);
		draw_string(S("save"), position.x + TAB_PADDING, position.y + TAB_PADDING, &font12, White);
	}

	if (!state->loaded)
	{
		draw_rectangle(object_group[Worlds].left - 1, object_group[Worlds].top - 1, object_group[Worlds].span_x + 2, object_group[Worlds].span_y + 2, display_colors[White]);
		for(i = 0; i < state->worlds->count; ++i)
		{
			if (i == object_group[Worlds].rows) break; // TODO scrolling support

			position = if_position(Worlds, i);
			if (state->world_index == i) // selected
			{
				fill_rectangle(position.x, position.y, object_group[Worlds].width, object_group[Worlds].height, display_colors[White]);
				draw_string(state->worlds->names[i]->data, state->worlds->names[i]->size, position.x, position.y + (object_group[Worlds].height - font12.height) / 2, &font12, Black);
			}
			else
			{
				draw_string(state->worlds->names[i]->data, state->worlds->names[i]->size, position.x, position.y + (object_group[Worlds].height - font12.height) / 2, &font12, White);
			}
		}
	}

	if (state->name_size)
		draw_string(state->name, state->name_size, object_group[Worlds].left, object_group[Worlds].bottom + MARGIN, &font12, White);
	draw_cursor(state->name, state->name_position, object_group[Worlds].left, object_group[Worlds].bottom + MARGIN, &font12, White);

	if (state->loaded)
	{
		for(i = 0; i < game->players_count; ++i)
		{
			if (i == PLAYER_NEUTRAL) continue;

			position = if_position(Players, i);

			show_flag(position.x, position.y, Player + i);
			switch (game->players[i].type)
			{
			case Local:
				draw_string(S("local player"), position.x + PLAYERS_INDICATOR_SIZE + PLAYERS_PADDING, position.y, &font12, White);
				break;

			case Computer:
				draw_string(S("Computer"), position.x + PLAYERS_INDICATOR_SIZE + PLAYERS_PADDING, position.y, &font12, White);
				break;
			}
		}
	}

	if (state->loaded)
	{
		show_button(S("Start game"), BUTTON_ENTER_X, BUTTON_ENTER_Y);
		show_button(S("Cancel"), BUTTON_CANCEL_X, BUTTON_CANCEL_Y);
	}
	else
	{
		if (state->name_size) show_button(S("Continue"), BUTTON_ENTER_X, BUTTON_ENTER_Y);
		else show_button(S(" "), BUTTON_ENTER_X, BUTTON_ENTER_Y);
		show_button(S("Exit"), BUTTON_EXIT_X, BUTTON_EXIT_Y);
	}
}

void if_save(const void *argument, const struct game *game)
{
	const struct state *state = argument;

	size_t i;

	struct point position;

	struct box box = string_box(S(TITLE_SAVE), &font24);
	draw_string(S(TITLE_SAVE), (SCREEN_WIDTH - box.width) / 2, TITLE_Y, &font24, White);

	// Display world directories tabs.
	position = if_position(WorldTabs, 0);
	draw_string(S("system"), position.x + TAB_PADDING, position.y + TAB_PADDING, &font12, White);
	position = if_position(WorldTabs, 1);
	draw_string(S("user"), position.x + TAB_PADDING, position.y + TAB_PADDING, &font12, White);
	position = if_position(WorldTabs, 2);
	draw_string(S("save"), position.x + TAB_PADDING, position.y + TAB_PADDING, &font12, White);

	draw_rectangle(object_group[Worlds].left - 1, object_group[Worlds].top - 1, object_group[Worlds].span_x + 2, object_group[Worlds].span_y + 2, display_colors[White]);
	for(i = 0; i < state->worlds->count; ++i)
	{
		if (i == object_group[Worlds].rows) break; // TODO scrolling support

		position = if_position(Worlds, i);
		if (state->world_index == i) // selected
		{
			fill_rectangle(position.x, position.y, object_group[Worlds].width, object_group[Worlds].height, display_colors[White]);
			draw_string(state->worlds->names[i]->data, state->worlds->names[i]->size, position.x, position.y + (object_group[Worlds].height - font12.height) / 2, &font12, Black);
		}
		else
		{
			draw_string(state->worlds->names[i]->data, state->worlds->names[i]->size, position.x, position.y + (object_group[Worlds].height - font12.height) / 2, &font12, White);
		}
	}

	if (state->name_size)
		draw_string(state->name, state->name_size, object_group[Worlds].left, object_group[Worlds].bottom + MARGIN, &font12, White);
	draw_cursor(state->name, state->name_position, object_group[Worlds].left, object_group[Worlds].bottom + MARGIN, &font12, White);

	if (state->error_size)
		draw_string(state->error, state->error_size, MENU_MESSAGE_X, MENU_MESSAGE_Y, &font12, Error);

	show_button(S("Save"), BUTTON_ENTER_X, BUTTON_ENTER_Y);
	show_button(S("Return to game"), BUTTON_CANCEL_X, BUTTON_CANCEL_Y);
	show_button(S("Quit game"), BUTTON_EXIT_X, BUTTON_EXIT_Y);
}
