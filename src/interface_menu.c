#include <GL/glx.h>

#include "base.h"
#include "map.h"
#include "input_menu.h"
#include "pathfinding.h"
#include "display.h"
#include "interface_menu.h"
#include "menu.h"
#include "world.h"

// TODO display long filenames properly

#define S(s) (s), sizeof(s) - 1

extern Display *display;
extern GLXDrawable drawable;

extern struct font font12;

void if_menu(const void *argument, const struct game *game)
{
	const struct state *state = argument;

	size_t i;

	struct point position;

	// TODO maybe use 2 separate functions for loaded and !loaded

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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
		draw_rectangle(object_group[Worlds].left - 1, object_group[Worlds].top - 1, object_group[Worlds].span_x + 2, object_group[Worlds].span_y + 2, White);
		for(i = 0; i < state->worlds->count; ++i)
		{
			if (i == object_group[Worlds].rows) break; // TODO scrolling support

			position = if_position(Worlds, i);
			if (state->world_index == i) // selected
			{
				fill_rectangle(position.x, position.y, object_group[Worlds].width, object_group[Worlds].height, White);
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
			fill_rectangle(position.x, position.y, PLAYERS_INDICATOR_SIZE, object_group[Players].height, Player + i); // TODO replace this with a flag
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

	glFlush();
	glXSwapBuffers(display, drawable);
}
