#include <X11/keysym.h>

#include "types.h"
#include "base.h"
#include "format.h"
#include "map.h"
#include "input.h"
#include "input_menu.h"
#include "pathfinding.h"
#include "display.h"
#include "interface_menu.h"
#include "menu.h"

// TODO handle long filenames properly

extern unsigned SCREEN_WIDTH, SCREEN_HEIGHT;

static int tab_select(struct state *restrict state, size_t index)
{
	// TODO is this behavior on error good?
	struct files *worlds_new = menu_worlds(index);
	if (!worlds_new) return -1; // TODO this could be several different errors

	menu_free(state->worlds);
	state->worlds = worlds_new;
	state->directory = index;
	state->world = -1;
	return 0;
}

static int input_none(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state *state = argument;

	// TODO support arrows for world selection

	switch (code)
	{
	case XK_Return:
		if (state->world >= 0) return INPUT_DONE;
		return 0;

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
			state->name[0] = 0;
			state->name_size = 0;
			state->name_position = 0;
		}
		state->world = -1;
		return 0;

	case XK_BackSpace:
		if (!state->name_position) return INPUT_IGNORE;

		if (state->name_position < state->name_size)
			memmove(state->name + state->name_position - 1, state->name + state->name_position, state->name_size - state->name_position);
		state->name_position -= 1;
		state->name_size -= 1;

		return 0;

	case XK_Delete:
		if (state->name_position == state->name_size) return INPUT_IGNORE;

		state->name_size -= 1;
		if (state->name_position < state->name_size)
			memmove(state->name + state->name_position, state->name + state->name_position + 1, state->name_size - state->name_position);

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
		const bytes_t *filename = state->worlds->names[index];
		state->world = index;
		format_bytes(state->name, filename->data, filename->size); // TODO buffer overflow
		state->name_position = state->name_size = filename->size;
		return 0;
	}

	return INPUT_IGNORE;

reset:
	if (state->world >= 0)
	{
		state->name_size = 0;
		state->name_position = 0;
		state->world = -1;
		return 0;
	}
	else return INPUT_IGNORE;
}

int input_load(struct game *restrict game)
{
	struct area areas[] = {
		{
			.left = 0,
			.right = SCREEN_WIDTH - 1,
			.top = 0,
			.bottom = SCREEN_HEIGHT - 1,
			.callback = input_none
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

	state.worlds = 0;
	if (tab_select(&state, 0) < 0) return -1; // TODO

	// TODO select some world by default (maybe the newest for saves and the oldest for other worlds)
	//#define WORLD_DEFAULT "worlds/balkans"
	state.name_size = 0;
	state.name_position = 0;

	while (1)
	{
		int status;

		if (status = input_local(areas, sizeof(areas) / sizeof(*areas), if_menu, game, &state)) return status;

		status = menu_load(state.directory, state.name, state.name_size, game);
		if (status == ERROR_MEMORY) return status;
		if (!status) break;

		// TODO indicate the error with something in red
		state.name_size = 0;
		state.name_position = 0;
	}

	// TODO customize player types, alliances, etc.

	return 0;
}

int input_save(const struct game *restrict game)
{
	struct area areas[] = {
		{
			.left = 0,
			.right = SCREEN_WIDTH - 1,
			.top = 0,
			.bottom = SCREEN_HEIGHT - 1,
			.callback = input_none
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

	state.worlds = 0;
	if (tab_select(&state, 2) < 0) return -1; // TODO

	// TODO ? generate some name
	state.name_size = 0;
	state.name_position = 0;

	int status = input_local(areas, sizeof(areas) / sizeof(*areas), if_menu, game, &state);
	free(state.worlds);
	if (status) return status;

	return menu_save(state.directory, state.name, state.name_size, game);
}
