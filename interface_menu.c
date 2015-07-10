#include <GL/glx.h>

#include "types.h"
#include "base.h"
#include "map.h"
#include "input_menu.h"
#include "pathfinding.h"
#include "display.h"
#include "interface_menu.h"
#include "menu.h"

// TODO display long filenames properly

extern Display *display;
extern GLXDrawable drawable;

extern struct font font12;

void if_menu(const void *argument, const struct game *game)
{
	const struct state *state = argument;

	size_t i;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	draw_rectangle(object_group[Worlds].left - 1, object_group[Worlds].top - 1, object_group[Worlds].span_x + 2, object_group[Worlds].span_y + 2, White);
	for(i = 0; i < state->worlds->count; ++i)
	{
		struct point position;

		if (i == object_group[Worlds].rows) break; // TODO scrolling support

		position = if_position(Worlds, i);
		if (state->world == i) // selected
		{
			fill_rectangle(position.x, position.y, object_group[Worlds].width, object_group[Worlds].height, White);
			display_string(state->worlds->names[i]->data, state->worlds->names[i]->size, position.x, position.y + (object_group[Worlds].height - font12.height) / 2, &font12, Black);
		}
		else
		{
			display_string(state->worlds->names[i]->data, state->worlds->names[i]->size, object_group[Worlds].left, object_group[Worlds].top + (object_group[Worlds].height - font12.height) / 2, &font12, White);
		}
	}

	display_string(state->filename, state->filename_size, object_group[Worlds].left, object_group[Worlds].bottom, &font12, White);

	glFlush();
	glXSwapBuffers(display, drawable);
}
