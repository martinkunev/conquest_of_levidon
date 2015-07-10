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

static int input_none(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state *state = argument;

	// TODO allow entering filename
	// TODO support arrows for world selection

	switch (code)
	{
	default:
		state->filename[0] = 0;
		state->filename_size = 0;
		state->world = -1;
		return 0;

	case ((255 << 8) | 13): // return
		if (state->world >= 0) return INPUT_DONE;
		else return 0;

	case ((255 << 8) | 27): // escape
		return INPUT_TERMINATE;

	case EVENT_MOTION:
		return INPUT_NOTME;
	}
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
		*format_bytes(state->filename, filename->data, filename->size) = 0; // TODO buffer overflow
		state->filename_size = filename->size;
		return 0;
	}

	return INPUT_IGNORE;

reset:
	if (state->world >= 0)
	{
		state->filename[0] = 0;
		state->filename_size = 0;
		state->world = -1;
		return 0;
	}
	else return INPUT_IGNORE;
}

#define DIRECTORY_WORLDS "/home/martin/dev/medieval/worlds/"
#define DIRECTORY_SAVE "/home/martin/dev/medieval/save/"

//#define WORLD_DEFAULT "worlds/balkans"

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
			.left = object_group[Worlds].left,
			.right = object_group[Worlds].right,
			.top = object_group[Worlds].top,
			.bottom = object_group[Worlds].bottom,
			.callback = input_world
		},
	};

	// TODO getcwd
	// TODO three directories: global worlds, current user's worlds, current user's saved worlds

	struct state state;

	state.worlds = menu_worlds(DIRECTORY_WORLDS, sizeof(DIRECTORY_WORLDS) - 1);
	if (!state.worlds) return -1; // TODO this could be several different errors

	state.world = -1; // TODO select some world by default (maybe the newest for saves and the oldest for other worlds)

	state.filename[0] = 0;
	state.filename_size = 0;

	int status = input_local(areas, sizeof(areas) / sizeof(*areas), if_menu, game, &state);
	if (status) return status;

	// TODO customize player types, alliances, etc.

	const bytes_t *world = state.worlds->names[state.world];

	return menu_load(DIRECTORY_WORLDS, sizeof(DIRECTORY_WORLDS) - 1, world->data, world->size, game);
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
			.left = object_group[Worlds].left,
			.right = object_group[Worlds].right,
			.top = object_group[Worlds].top,
			.bottom = object_group[Worlds].bottom,
			.callback = input_world
		},
	};

	// TODO getcwd

	struct state state;

	state.worlds = menu_worlds(DIRECTORY_SAVE, sizeof(DIRECTORY_SAVE) - 1);
	if (!state.worlds) return -1; // TODO this could be several different errors

	state.world = -1;

	// TODO ? generate some name
	state.filename[0] = 0;
	state.filename_size = 0;

	int status = input_local(areas, sizeof(areas) / sizeof(*areas), if_menu, game, &state);
	if (status) return status;

	return menu_save(DIRECTORY_SAVE, sizeof(DIRECTORY_SAVE) - 1, state.filename, state.filename_size, game);
}
