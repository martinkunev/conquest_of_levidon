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

#include <pthread.h>
#include <X11/keysym.h>

#include "errors.h"
#include "base.h"
#include "format.h"
#include "game.h"
#include "draw.h"
#include "map.h"
#include "input.h"
#include "input_menu.h"
#include "pathfinding.h"
#include "display_common.h"
#include "display_menu.h"
#include "menu.h"
#include "world.h"

#define ERROR_STRING_SAVE "Unable to save game"

// TODO handle long filenames properly

extern unsigned WINDOW_WIDTH, WINDOW_HEIGHT;

static int tab_select(struct state *restrict state, size_t index)
{
	// TODO is this behavior on error good?
	struct files *worlds_new = menu_worlds(index);
	if (!worlds_new) return -1; // TODO this could be several different errors

	menu_free(state->worlds);
	state->worlds = worlds_new;
	state->directory = index;
	state->world_index = -1;
	state->name_size = 0;
	state->name_position = 0;
	return 0;
}

static void world_select(struct state *restrict state, size_t index)
{
	const struct bytes *filename = state->worlds->names[index];
	state->world_index = index;
	format_bytes(state->name, filename->data, filename->size); // TODO buffer overflow
	state->name_position = state->name_size = filename->size;
}

static int input_continue(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state *state = argument;
	if (code != EVENT_MOUSE_LEFT) return INPUT_NOTME;
	if (state->name_size) return INPUT_FINISH;
	else return 0;
}

static int input_none(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state *state = argument;

	// Ignore input if control, alt or super is pressed.
	// TODO is this necessary?
	if (modifiers & (XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1 | XCB_MOD_MASK_4)) return INPUT_NOTME;

	switch (code)
	{
	case XK_Up:
		if (state->world_index == 0) return INPUT_IGNORE;
		if (state->world_index < 0) world_select(state, state->worlds->count - 1);
		else world_select(state, state->world_index - 1);
		return 0;

	case XK_Down:
		if (state->world_index == (state->worlds->count - 1)) return INPUT_IGNORE;
		if (state->world_index < 0) world_select(state, 0);
		else world_select(state, state->world_index + 1);
		return 0;

	case XK_Return:
		return input_continue(EVENT_MOUSE_LEFT, x, y, modifiers, game, argument);

	case 'q': // TODO isn't this buggy
	case XK_Escape:
		return INPUT_TERMINATE;

	case EVENT_MOTION:
		return INPUT_NOTME;

	default:
		if ((code > 31) && (code < 127))
		{
			if (state->name_size == FILENAME_LIMIT) return INPUT_IGNORE; // TODO indicate the buffer is full

			if (state->name_position < state->name_size)
				memmove(state->name + state->name_position + 1, state->name + state->name_position, state->name_size - state->name_position);
			state->name[state->name_position++] = code;
			state->name_size += 1;
		}
		else
		{
			state->name_size = 0;
			state->name_position = 0;
		}
		state->world_index = -1;
		return 0;

	case XK_BackSpace:
		if (!state->name_position) return INPUT_IGNORE;

		if (state->name_position < state->name_size)
			memmove(state->name + state->name_position - 1, state->name + state->name_position, state->name_size - state->name_position);
		state->name_position -= 1;
		state->name_size -= 1;
		state->world_index = -1;

		return 0;

	case XK_Delete:
		if (state->name_position == state->name_size) return INPUT_IGNORE;

		state->name_size -= 1;
		if (state->name_position < state->name_size)
			memmove(state->name + state->name_position, state->name + state->name_position + 1, state->name_size - state->name_position);
		state->world_index = -1;

		return 0;

	case XK_Home:
		if (state->name_position == 0) return INPUT_IGNORE;
		state->name_position = 0;
		return 0;

	case XK_End:
		if (state->name_position == state->name_size) return INPUT_IGNORE;
		state->name_position = state->name_size;
		return 0;

	case XK_Left:
		if (state->name_position == 0) return INPUT_IGNORE;
		state->name_position -= 1;
		return 0;

	case XK_Right:
		if (state->name_position == state->name_size) return INPUT_IGNORE;
		state->name_position += 1;
		return 0;
	}
}

static int input_write(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state *state = argument;
	int status;

	if (code != EVENT_MOUSE_LEFT) return INPUT_NOTME;

	status = menu_save(state->directory, state->name, state->name_size, game);
	if (status == ERROR_MEMORY) return ERROR_MEMORY;
	if (!status) return INPUT_FINISH;

	state->error = ERROR_STRING_SAVE;
	state->error_size = sizeof(ERROR_STRING_SAVE) - 1;

	return 0;
}

static int input_menu(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	//struct state *state = argument;

	// Ignore input if control, alt or super is pressed.
	// TODO is this necessary?
	if (modifiers & (XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1 | XCB_MOD_MASK_4)) return INPUT_NOTME;

	switch (code)
	{
	case XK_Return:
		return input_write(EVENT_MOUSE_LEFT, x, y, modifiers, game, argument);
	case XK_Escape:
		return INPUT_FINISH;
	default:
		return INPUT_NOTME;
	}
}

static int input_tab(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state *state = argument;

	ssize_t index;

	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME; // ignore keyboard events

	// Find which tab was clicked.
	index = if_index(WorldTabs, (struct point){x, y});
	if ((index < 0) || (index >= DIRECTORIES_COUNT)) return INPUT_IGNORE; // no tab clicked

	if (code == EVENT_MOUSE_LEFT)
	{
		if (tab_select(state, index) < 0) return INPUT_IGNORE; // TODO
		return 0;
	}

	return INPUT_IGNORE;
}

static int input_world(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state *state = argument;

	ssize_t index;

	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME; // ignore keyboard events

	// Find which world was clicked.
	index = if_index(Worlds, (struct point){x, y});
	if ((index < 0) || (index >= state->worlds->count)) goto reset; // no world clicked

	// TODO scrolling

	if (code == EVENT_MOUSE_LEFT)
	{
		world_select(state, index);
		return 0;
	}

	return INPUT_IGNORE;

reset:
	if (state->world_index >= 0)
	{
		state->name_size = 0;
		state->name_position = 0;
		state->world_index = -1;
		return 0;
	}
	else return INPUT_IGNORE;
}

static int input_setup(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	//struct state *state = argument;

	switch (code)
	{
	case XK_Return:
		return INPUT_FINISH;
	case XK_Escape:
		return INPUT_TERMINATE;
	default:
		return INPUT_NOTME;
	}
}

static int input_player(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	//struct state *state = argument;

	ssize_t index;

	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME; // ignore keyboard events

	// TODO customize player alliances, colors, etc.

	// Find which player was clicked. Ignore the neutral player.
	index = if_index(Players, (struct point){x, y});
	if ((index < 0) || (index >= game->players_count)) return INPUT_IGNORE; // no player clicked
	if (index == PLAYER_NEUTRAL) return INPUT_IGNORE;

	if (code == EVENT_MOUSE_LEFT)
	{
		if (game->players[index].type == Local)
		{
			// Make sure there is another player of type Local.
			size_t i;
			for(i = 0; i < game->players_count; ++i)
			{
				if (i == index) continue;
				if (game->players[i].type == Local)
				{
					game->players[index].type = Computer;
					return 0;
				}
			}
			return INPUT_IGNORE;
		}
		else
		{
			game->players[index].type = Local;
			return 0;
		}
	}

	return INPUT_IGNORE;
}

int input_load(struct game *restrict game)
{
	struct area areas[] = {
		{
			.left = 0,
			.right = WINDOW_WIDTH - 1,
			.top = 0,
			.bottom = WINDOW_HEIGHT - 1,
			.callback = input_none
		},
		{
			.left = BUTTON_ENTER_X,
			.right = BUTTON_ENTER_X + BUTTON_WIDTH,
			.top = BUTTON_ENTER_Y,
			.bottom = BUTTON_ENTER_Y + BUTTON_HEIGHT,
			.callback = input_continue
		},
		{
			.left = BUTTON_EXIT_X,
			.right = BUTTON_EXIT_X + BUTTON_WIDTH,
			.top = BUTTON_EXIT_Y,
			.bottom = BUTTON_EXIT_Y + BUTTON_HEIGHT,
			.callback = input_terminate
		},
		{
			.left = object_group[WorldTabs].left,
			.right = object_group[WorldTabs].right,
			.top = object_group[WorldTabs].top,
			.bottom = object_group[WorldTabs].bottom,
			.callback = input_tab
		},
		{
			.left = object_group[Worlds].left,
			.right = object_group[Worlds].right,
			.top = object_group[Worlds].top,
			.bottom = object_group[Worlds].bottom,
			.callback = input_world
		},
	};

	struct area areas_setup[] = {
		{
			.left = 0,
			.right = WINDOW_WIDTH - 1,
			.top = 0,
			.bottom = WINDOW_HEIGHT - 1,
			.callback = input_setup
		},
		{
			.left = BUTTON_ENTER_X,
			.right = BUTTON_ENTER_X + BUTTON_WIDTH,
			.top = BUTTON_ENTER_Y,
			.bottom = BUTTON_ENTER_Y + BUTTON_HEIGHT,
			.callback = input_finish
		},
		{
			.left = BUTTON_CANCEL_X,
			.right = BUTTON_CANCEL_X + BUTTON_WIDTH,
			.top = BUTTON_CANCEL_Y,
			.bottom = BUTTON_CANCEL_Y + BUTTON_HEIGHT,
			.callback = input_terminate
		},
		{
			.left = object_group[Players].left,
			.right = object_group[Players].right,
			.top = object_group[Players].top,
			.bottom = object_group[Players].bottom,
			.callback = input_player
		},
	};

	struct state state;
	int status;

	state.worlds = 0;
	if (tab_select(&state, 0) < 0) return -1; // TODO

	// TODO select some world by default (maybe the newest for saves and the oldest for other worlds)
	//#define WORLD_DEFAULT "worlds/balkans"

retry:
	state.loaded = 0;

	while (1)
	{
		status = input_local(areas, sizeof(areas) / sizeof(*areas), if_load, game, &state);
		if (status) goto finally;

		status = menu_load(state.directory, state.name, state.name_size, game);
		if (status == ERROR_MEMORY) goto finally;
		if (!status) break;

		// TODO indicate the error with something in red
		state.name_size = 0;
		state.name_position = 0;
		state.world_index = -1;
	}

	state.loaded = 1;

	status = input_local(areas_setup, sizeof(areas_setup) / sizeof(*areas_setup), if_load, game, &state);
	if (status == ERROR_CANCEL)
	{
		world_unload(game);
		goto retry;
	}

finally:
	menu_free(state.worlds);
	return status;
}

int input_save(const struct game *restrict game)
{
	struct area areas[] = {
		{
			.left = 0,
			.right = WINDOW_WIDTH - 1,
			.top = 0,
			.bottom = WINDOW_HEIGHT - 1,
			.callback = input_none
		},
		{
			.left = BUTTON_ENTER_X,
			.right = BUTTON_ENTER_X + BUTTON_WIDTH,
			.top = BUTTON_ENTER_Y,
			.bottom = BUTTON_ENTER_Y + BUTTON_HEIGHT,
			.callback = input_write
		},
		{
			.left = BUTTON_CANCEL_X,
			.right = BUTTON_CANCEL_X + BUTTON_WIDTH,
			.top = BUTTON_CANCEL_Y,
			.bottom = BUTTON_CANCEL_Y + BUTTON_HEIGHT,
			.callback = input_finish
		},
		{
			.left = BUTTON_EXIT_X,
			.right = BUTTON_EXIT_X + BUTTON_WIDTH,
			.top = BUTTON_EXIT_Y,
			.bottom = BUTTON_EXIT_Y + BUTTON_HEIGHT,
			.callback = input_terminate
		},
		{
			.left = 0,
			.right = WINDOW_WIDTH - 1,
			.top = 0,
			.bottom = WINDOW_HEIGHT - 1,
			.callback = input_menu
		},
		{
			.left = object_group[WorldTabs].left,
			.right = object_group[WorldTabs].right,
			.top = object_group[WorldTabs].top,
			.bottom = object_group[WorldTabs].bottom,
			.callback = input_tab
		},
		{
			.left = object_group[Worlds].left,
			.right = object_group[Worlds].right,
			.top = object_group[Worlds].top,
			.bottom = object_group[Worlds].bottom,
			.callback = input_world
		},
	};

	struct state state;
	int status;

	state.worlds = 0;
	if (tab_select(&state, 2) < 0) return -1; // TODO

	state.loaded = 0;

	state.error_size = 0;

	// TODO ? generate some filename

	status = input_local(areas, sizeof(areas) / sizeof(*areas), if_save, game, &state);

finally:
	menu_free(state.worlds);
	return status;
}
