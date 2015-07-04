struct game;

#define GL_GLEXT_PROTOTYPES

#include <stdlib.h>

#include <GL/glx.h>
#include <GL/glext.h>

#include <xcb/xcb.h>

#include "types.h"
#include "input.h"
#include "image.h"
#include "draw.h"
#include "interface.h"

struct vertex
{
	struct point point;
	int previous; // the previous vertex with the same point
	unsigned region;
};

#define array_type struct vertex
#include "array.h"
#include "array.c"

#include <stdio.h>

extern Display *display;
extern GLXDrawable drawable;
extern xcb_screen_t *screen;
extern xcb_connection_t *connection;
extern KeySym *keymap;
extern int keysyms_per_keycode;
extern int keycode_min, keycode_max;

extern struct font font12;

static GLuint map_framebuffer;

static GLuint map_renderbuffer;

struct state
{
	struct array points;
	unsigned region_start;
	unsigned region;
};

static struct image image_world;

static void if_storage_init(void)
{
	glGenRenderbuffers(1, &map_renderbuffer);
	glGenFramebuffers(1, &map_framebuffer);

	glBindRenderbuffer(GL_RENDERBUFFER, map_renderbuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, map_framebuffer);

	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB8, image_world.width, image_world.height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, map_renderbuffer);

	glViewport(0, 0, image_world.width, image_world.height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, image_world.width, 0, image_world.height, 0, 1);

	glClear(GL_COLOR_BUFFER_BIT);

	glFlush();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void colored_rectangle(unsigned x, unsigned y, unsigned width, unsigned height)
{
	glBegin(GL_QUADS);
	glVertex2i(x + width, y + height);
	glVertex2i(x + width, y);
	glVertex2i(x, y);
	glVertex2i(x, y + height);
	glEnd();
}

static void if_storage_point(unsigned x, unsigned y, int index)
{
	glViewport(0, 0, image_world.width, image_world.height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, image_world.width, 0, image_world.height, 0, 1);

	glBindFramebuffer(GL_FRAMEBUFFER, map_framebuffer);

	if (index < 0) glColor3ub(0, 0, 0);
	else glColor3ub(255, index / 256, index % 256);
	colored_rectangle(x - 6, y - 6, 13, 13);

	glFlush();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glViewport(0, 0, screen->width_in_pixels, screen->height_in_pixels);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, screen->width_in_pixels, screen->height_in_pixels, 0, 0, 1);
}

static int if_storage_get(unsigned x, unsigned y)
{
	GLubyte pixel[3];

	glBindFramebuffer(GL_READ_FRAMEBUFFER, map_framebuffer);
	glReadPixels(x, y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	if (!pixel[0]) return -1;
	return pixel[1] * 256 + pixel[2];
}

static void if_storage_term(void)
{
	glDeleteRenderbuffers(1, &map_renderbuffer);
	glDeleteFramebuffers(1, &map_framebuffer);
}

static int input_editor(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state *state = argument;

	// TODO hover

	switch (code)
	{
	default:
		return INPUT_IGNORE;

	case 'q':
		return INPUT_DONE;

	case EVENT_MOUSE_LEFT:
		{
			int index = if_storage_get(x, y);
			if (index > (int)state->region_start) // this point is already added to the current region
				return INPUT_IGNORE;
			else if (index == (int)state->region_start)
			{
				if (state->region_start > state->points.count - 3)
					return INPUT_IGNORE;
				state->region_start = state->points.count;
				state->region += 1;
				return 0;
			}

			if (index >= 0)
			{
				x = state->points.data[index].point.x;
				y = state->points.data[index].point.y;
			}

			if_storage_point(x, y, state->points.count);
			if (array_push(&state->points, (struct vertex){x, y, index, state->region}) < 0)
				; // TODO

			//printf("%u %u\n", x, y);
		}
		return 0;

	case EVENT_MOUSE_RIGHT:
		{
			size_t i;

			int index = if_storage_get(x, y);
			if (index < 0) return INPUT_IGNORE;
			if (index < state->region_start) return INPUT_IGNORE;

			x = state->points.data[index].point.x;
			y = state->points.data[index].point.y;
			if_storage_point(x, y, state->points.data[index].previous);

			state->points.count -= 1;
			for(i = index; i < state->points.count; ++i)
				state->points.data[i] = state->points.data[i + 1];

			//printf("%u %u\n", x, y);
		}
		return 0;

	/*case ' ':
		if (state->region_start > state->points.count - 3)
			return INPUT_IGNORE;
		state->region_start = state->points.count;
		state->region += 1;
		return 0;*/

	case ((255 << 8) | 27): // escape
		if (state->points.count <= state->region_start) return INPUT_IGNORE;
		state->points.count -= 1;
		return 0;
	}
}

static void if_editor(const void *argument, const struct game *game)
{
	const struct state *state = argument;

	size_t i;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	image_draw(&image_world, 0, 0);

	// Draw the contours of the regions.
	unsigned region_start = 0;
	unsigned region = 0;
	for(i = 1; i < state->points.count; ++i)
	{
		if (state->points.data[i].region != region)
		{
			struct point start = state->points.data[i - 1].point;
			struct point end = state->points.data[region_start].point;

			glColor3ub(0, 0, 0);
			glBegin(GL_LINES);
			glVertex2f(start.x, start.y);
			glVertex2f(end.x, end.y);
			glEnd();

			region = state->points.data[i].region;
			region_start = i;

			continue;
		}

		struct point start = state->points.data[i - 1].point;
		struct point end = state->points.data[i].point;

		glColor3ub(0, 0, 0);
		glBegin(GL_LINES);
		glVertex2f(start.x, start.y);
		glVertex2f(end.x, end.y);
		glEnd();
	}

	// Display regions vertices.
	for(i = 0; i < state->region_start; ++i)
	{
		struct point point = state->points.data[i].point;
		draw_rectangle(point.x - 3, point.y - 3, 7, 7, Black);
		display_rectangle(point.x - 2, point.y - 2, 5, 5, Ally);
	}
	for(; i < state->points.count; ++i)
	{
		struct point point = state->points.data[i].point;
		draw_rectangle(point.x - 3, point.y - 3, 7, 7, Black);
		display_rectangle(point.x - 2, point.y - 2, 5, 5, Enemy);
	}

	glFlush();
	glXSwapBuffers(display, drawable);
}

static void input(void)
{
	extern unsigned SCREEN_WIDTH, SCREEN_HEIGHT;

	struct area areas[] = {
		{
			.left = 0,
			.right = SCREEN_WIDTH - 1,
			.top = 0,
			.bottom = SCREEN_HEIGHT - 1,
			.callback = input_editor
		}
	};

	struct state state;

	if (array_init(&state.points, ARRAY_SIZE_DEFAULT) < 0)
		return; // TODO

	state.region_start = 0;
	state.region = 0;

	input_local(areas, sizeof(areas) / sizeof(*areas), if_editor, 0, &state);
}

int main(void)
{
	if_init();

	image_load_png(&image_world, "img/world.png", 0);
	if_storage_init();

	if_display();

	input();

	if_storage_term();

	if_term();

	return 0;
}
