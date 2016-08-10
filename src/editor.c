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

#define GL_GLEXT_PROTOTYPES

#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <GL/glx.h>
#include <GL/glext.h>

#include "errors.h"
#include "log.h"
#include "format.h"
#include "json.h"
#include "game.h"
#include "draw.h"
#include "map.h"
#include "interface.h"
#include "input.h"
#include "image.h"
#include "pathfinding.h"
#include "display_common.h"
#include "world.h"

#define EDITOR_X 0
#define EDITOR_Y 0

#define BUTTON_REGIONS_X 780
#define BUTTON_REGIONS_Y 16

#define BUTTON_POINTS_X 780
#define BUTTON_POINTS_Y 36

#define REGIONNAME_X 780
#define REGIONNAME_Y 128

#define CENTER_WIDTH 8
#define CENTER_HEIGHT 8

#define OBJECT_X 920
#define OBJECT_Y 16

#define WORLD_TEMP "/tmp/conquest_of_levidon_world"
#define PREFIX_IMG PREFIX "share/conquest_of_levidon/img/"

#define S(s) (s), sizeof(s) - 1

// WARNING: Vertices in a region must be listed counterclockwise.
// WARNING: Each region can have up to 8 neighbors.

enum {TOOL_REGIONS, TOOL_POINTS} editor_tool;

struct vertex
{
	struct point point;
	int previous; // the previous vertex with the same point
	unsigned region;
};

// TODO FIXED memory corruption overwrites neighbors pointers (sometimes)
// TODO FIXED buggy region coloring (most likely due to the memory corruption)

#define array_name array_vertex
#define array_type struct vertex
#include "generic/array.g"

static GLuint map_framebuffer;
static GLuint map_renderbuffer;

extern Display *display;
extern GLXDrawable drawable;

struct state
{
	// TOOL_REGIONS
	unsigned char *colors;
	ssize_t region_index;
	char name[NAME_LIMIT];
	size_t name_size;
	size_t name_position;
	enum {OBJECT_GARRISON, OBJECT_CENTER} object;

	// TOOL_POINTS
	struct array_vertex points;
	size_t index_start;
};

static struct image image_world;
static struct image image_garrison, image_village;
static struct image image_garrison_gray, image_village_gray;

/* < Interface storage */
// TODO put this in a separate file

static void if_storage_init(unsigned width, unsigned height)
{
	glGenRenderbuffers(1, &map_renderbuffer);
	glGenFramebuffers(1, &map_framebuffer);

	glBindRenderbuffer(GL_RENDERBUFFER, map_renderbuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, map_framebuffer);

	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB8, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, map_renderbuffer);

	glViewport(0, 0, width, height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, width, 0, height, 0, 1);

	glClear(GL_COLOR_BUFFER_BIT);

	glFlush();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void colored_rectangle(unsigned x, unsigned y, unsigned width, unsigned height, const unsigned char color[4])
{
	glColor4ubv(color);

	glBegin(GL_QUADS);
	glVertex2i(x + width, y + height);
	glVertex2i(x + width, y);
	glVertex2i(x, y);
	glVertex2i(x, y + height);
	glEnd();
}

static void if_index_color(unsigned char color[4], int index)
{
	if (index < 0)
	{
		color[0] = 0;
		color[1] = 0;
		color[2] = 0;
	}
	else
	{
		color[0] = 255;
		color[1] = index / 256;
		color[2] = index % 256;
	}
	color[3] = 255;
}

static void if_switch_buffer(void)
{
	glBindFramebuffer(GL_FRAMEBUFFER, map_framebuffer);

	glViewport(0, 0, image_world.width, image_world.height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, image_world.width, 0, image_world.height, 0, 1);
}

static void if_switch_screen(void)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 1);
}

static void if_storage_region(const struct polygon *restrict location, int index)
{
	unsigned char color[4];
	if_index_color(color, index);
	fill_polygon(location, 0, 0, color);
}

static void if_storage_point(unsigned x, unsigned y, int index)
{
	unsigned char color[4];
	if_switch_buffer();
	if_index_color(color, index);
	colored_rectangle(x - 6, y - 6, 13, 13, color);
	glFlush();
	if_switch_screen();
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

/* Interface storage > */

static int regions_color_try(const struct game *restrict game, unsigned char *colors, size_t region_index)
{
	size_t i;

	unsigned char used[4] = {0}; // the map can be colored with just 4 colors
	const struct region *restrict region = game->regions + region_index;

	if (region_index == game->regions_count) return 0;

	// Find which colors are already used by the neighbors of the region.
	for(i = 0; i < NEIGHBORS_LIMIT; ++i)
	{
		const struct region *restrict neighbor = region->neighbors[i];

		if (!neighbor) continue; // no such neighbor
		if (neighbor->index >= region_index) continue; // not colored yet

		used[colors[neighbor->index]] = 1;
	}

	// Color the region with one of the non-used colors (with backtracking).
	for(i = 0; i < sizeof(used) / sizeof(*used); ++i)
	{
		if (used[i]) continue;

		colors[region_index] = i;
		if (!regions_color_try(game, colors, region_index + 1))
			return 0;
	}

	return -1;
}

// Returns an array of color indices (one for each region of the map).
// Makes sure no two neighboring regions have the same color.
static inline unsigned char *regions_colors(const struct game *restrict game)
{
	unsigned char *colors = malloc(game->regions_count);
	if (!colors) return 0;

	regions_color_try(game, colors, 0); // never fails for a valid map (proven by 4 color theorem)

	return colors;
}

static void region_init(struct game *restrict game, struct region *restrict region, const struct vertex *restrict points, size_t points_count)
{
	static unsigned regions_created_count = 0;

	size_t i;

	// Set region number as a name.
	region->name_length = format_uint(region->name, regions_created_count, 10) - (uint8_t *)region->name;
	regions_created_count += 1;

	region->index = game->regions_count;
	for(i = 0; i < NEIGHBORS_LIMIT; ++i)
		region->neighbors[i] = 0;

	region->location = malloc(offsetof(struct polygon, points) + points_count * sizeof(struct point));
	if (!region->location) abort();

	for(i = 0; i < points_count; ++i)
		region->location->points[i] = points[i].point;
	region->location->vertices_count = points_count;

	region->location_garrison = region->location->points[0];
	region->center = region->location->points[0];

	region->owner = 0;
	region->train_progress = 0;
	for(i = 0; i < TRAIN_QUEUE; ++i)
		region->train[i] = 0;
	region->troops = 0;
	region->garrison.owner = 0;
	region->garrison.siege = 0;
	region->built = 0;
	region->construct = -1;
	region->build_progress = 0;
}

static void tool_regions_init(struct game *restrict game, struct state *restrict state)
{
	size_t i;

	editor_tool = TOOL_REGIONS;

	state->region_index = -1;
	memset(state->name, 0, sizeof(state->name));
	state->name_size = 0;
	state->name_position = 0;
	state->object = OBJECT_GARRISON;

	if_switch_buffer();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	for(i = 0; i < game->regions_count; ++i)
		if_storage_region(game->regions[i].location, i);
	glFlush();

	if_switch_screen();

	state->colors = regions_colors(game);
	if (!state->colors) abort();
}

static void tool_regions_term(struct game *restrict game, struct state *restrict state)
{
	free(state->colors);
}

static void tool_points_init(const struct game *restrict game, struct state *restrict state)
{
	size_t i, j;

	editor_tool = TOOL_POINTS;

	if_switch_buffer();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	state->points = (struct array_vertex){0};
	for(i = 0; i < game->regions_count; ++i)
	{
		const struct polygon *restrict location = game->regions[i].location;
		struct vertex *points;

		if (array_vertex_expand(&state->points, state->points.count + location->vertices_count) < 0)
			abort();
		points = state->points.data;

		for(j = 0; j < location->vertices_count; ++j)
		{
			unsigned x = location->points[j].x;
			unsigned y = location->points[j].y;

			int index = if_storage_get(x, y);
			if (index >= 0) // there is an existing point at this location
			{
				x = points[index].point.x;
				y = points[index].point.y;
			}

			if_storage_point(x, y, state->points.count);

			points[state->points.count].point = location->points[j];
			points[state->points.count].previous = index;
			points[state->points.count].region = i;
			state->points.count += 1;
		}
	}
	glFlush();

	if_switch_screen();

	state->index_start = state->points.count;
}

static void add_neighbor(struct region *restrict region, struct region *restrict neighbor)
{
	size_t i;
	for(i = 0; i < NEIGHBORS_LIMIT; ++i)
		if (!region->neighbors[i])
		{
			region->neighbors[i] = neighbor;
			return;
		}

	LOG_ERROR("Region %u has more than %u neighbors.", (unsigned)region->index, (unsigned)NEIGHBORS_LIMIT);
}

static void neighbors_generate(struct game *restrict game)
{
	size_t i, j;

	for(i = 0; i < game->regions_count; ++i)
		for(j = 0; j < NEIGHBORS_LIMIT; ++j)
			game->regions[i].neighbors[j] = 0;

	// Determine the neighbors of each region.
	for(i = 1; i < game->regions_count; ++i)
		for(j = 0; j < i; ++j)
		{
			struct region *restrict region = game->regions + j;
			struct region *restrict neighbor = game->regions + i;

			if (polygons_border(region->location, neighbor->location, 0, 0))
			{
				add_neighbor(region, neighbor);
				add_neighbor(neighbor, region);
			}
		}
}

static void tool_points_term(struct game *restrict game, struct state *restrict state)
{
	if (state->points.count > state->index_start) // a new region was just added
	{
		struct region *restrict region;

		void *buffer = realloc(game->regions, (game->regions_count + 1) * sizeof(*game->regions));
		if (!buffer) abort();
		game->regions = buffer;

		region = game->regions + game->regions_count;
		region_init(game, region, state->points.data + state->index_start, state->points.count - state->index_start);

		game->regions_count += 1;

		// Re-generate neighbors since the pointers are not valid after the realloc and the new region has no neighbors set.
		neighbors_generate(game);
	}

	free(state->points.data);
}

static int input_region(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state *state = argument;

	switch (code)
	{
	case EVENT_MOTION:
		return INPUT_IGNORE;

	case XK_Escape:
		// Exit the editor.
		return INPUT_TERMINATE;

	case EVENT_MOUSE_LEFT:
		{
			int index;

			if ((x >= MAP_WIDTH) || (y >= MAP_HEIGHT)) return INPUT_IGNORE;

			index = if_storage_get(x, y);
			if (index == state->region_index) return INPUT_IGNORE;
			if (index < 0)
			{
				state->region_index = index;
				return 0;
			}

			state->region_index = index;
			state->name_size = game->regions[index].name_length;
			memcpy(state->name, game->regions[index].name, state->name_size);
			state->name_position = state->name_size;
		}
		return 0;

	case EVENT_MOUSE_RIGHT:
		{
			struct region *restrict region;

			if ((x >= MAP_WIDTH) || (y >= MAP_HEIGHT)) return INPUT_IGNORE;

			if (state->region_index < 0) return INPUT_IGNORE;
			region = game->regions + state->region_index;

			switch (state->object)
			{
			case OBJECT_GARRISON:
				region->location_garrison = (struct point){x, y};
				break;

			case OBJECT_CENTER:
				region->center = (struct point){x, y};
				break;
			}
		}
		return 0;

	default:
		if (state->region_index < 0) return INPUT_IGNORE;

		if ((code > 31) && (code < 127))
		{
			if (state->name_size == NAME_LIMIT) return INPUT_IGNORE; // TODO indicate that the buffer is full

			// Handle capital letters.
			if ((modifiers & XCB_MOD_MASK_SHIFT) && islower(code))
				code = toupper(code);

			if (state->name_position < state->name_size)
				memmove(state->name + state->name_position + 1, state->name + state->name_position, state->name_size - state->name_position);
			state->name[state->name_position++] = code;
			state->name_size += 1;
		}

		return 0;

	case XK_BackSpace:
		if (!state->name_position) return INPUT_IGNORE;

		if (state->name_position < state->name_size)
			memmove(state->name + state->name_position - 1, state->name + state->name_position, state->name_size - state->name_position);
		state->name_position -= 1;
		state->name_size -= 1;

		return 0;

	case XK_Delete:
		if (modifiers & XCB_MOD_MASK_SHIFT)
		{
			struct game *restrict game_mutable = (struct game *)game; // TODO fix cast

			if (state->region_index < 0) return INPUT_IGNORE;

			// Remove selected region.
			game_mutable->regions_count -= 1;
			if (state->region_index != game_mutable->regions_count)
				memcpy(game_mutable->regions + state->region_index, game_mutable->regions + game_mutable->regions_count, sizeof(*game_mutable->regions));

			neighbors_generate(game_mutable);
		}
		else
		{
			if (state->name_position == state->name_size) return INPUT_IGNORE;

			state->name_size -= 1;
			if (state->name_position < state->name_size)
				memmove(state->name + state->name_position, state->name + state->name_position + 1, state->name_size - state->name_position);
		}
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

	case XK_Return:
		{
			struct region *restrict region = game->regions + state->region_index;
			memcpy(region->name, state->name, state->name_size);
			region->name_length = state->name_size;
			state->region_index = -1;
		}
		return 0;
	}
}

static int input_tool_points(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state *restrict state = argument;

	switch (code)
	{
	default:
		return INPUT_IGNORE;

	case EVENT_MOUSE_LEFT:
		tool_regions_term((struct game *)game, state); // TODO fix cast
		tool_points_init(game, state);
		return INPUT_FINISH;
	}
}

static int input_point(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state *state = argument;

	// TODO hover?

	switch (code)
	{
	default:
		return INPUT_IGNORE;

	case XK_Escape:
		// Cancel points input.
		state->points.count = state->index_start;
		tool_points_term((struct game *)game, state); // TODO fix cast
		tool_regions_init((struct game *)game, state); // TODO fix cast
		return INPUT_FINISH;

	case XK_Delete:
		{
			struct vertex v;

			if (state->points.count <= state->index_start)
				return INPUT_IGNORE;

			v = state->points.data[state->points.count - 1];
			if_storage_point(v.point.x, v.point.y, v.previous);
			state->points.count -= 1;
		}
		return 0;

	case EVENT_MOUSE_LEFT:
		{
			int index;

			if ((x >= MAP_WIDTH) || (y >= MAP_HEIGHT)) return INPUT_IGNORE;

			index = if_storage_get(x, y);
			if (index > (int)state->index_start) // this point is already added to the current region
				return INPUT_IGNORE;

			// If the user clicks on the first point of the region, the region is ready for creation.
			if (index == (int)state->index_start)
			{
				if (state->index_start > state->points.count - 3) // at least 3 points are required to form a region
					return INPUT_IGNORE;

				tool_points_term((struct game *)game, state); // TODO fix cast
				tool_regions_init((struct game *)game, state); // TODO fix cast
				return INPUT_FINISH;
			}

			if (index >= 0) // there is an existing point at this location
			{
				x = state->points.data[index].point.x;
				y = state->points.data[index].point.y;
			}

			if_storage_point(x, y, state->points.count);
			if (array_vertex_expand(&state->points, state->points.count + 1) < 0)
				abort();
			state->points.data[state->points.count++] = (struct vertex){x, y, index, game->regions_count};
		}
		return 0;

	case EVENT_MOUSE_RIGHT:
		{
			size_t i;
			int index;

			if ((x >= MAP_WIDTH) || (y >= MAP_HEIGHT)) return INPUT_IGNORE;

			index = if_storage_get(x, y);
			if (index < 0) return INPUT_IGNORE;
			if (index < (int)state->index_start) return INPUT_IGNORE;

			x = state->points.data[index].point.x;
			y = state->points.data[index].point.y;
			if_storage_point(x, y, state->points.data[index].previous);

			state->points.count -= 1;
			for(i = index; i < state->points.count; ++i)
				state->points.data[i] = state->points.data[i + 1];
		}
		return 0;
	}
}

static int input_tool_regions(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state *restrict state = argument;

	switch (code)
	{
	default:
		return INPUT_IGNORE;

	case EVENT_MOUSE_LEFT:
		tool_points_term((struct game *)game, state); // TODO fix cast
		tool_regions_init((struct game *)game, state); // TODO fix cast
		return INPUT_FINISH;
	}
}

static int input_object_garrison(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state *restrict state = argument;
	if (code != EVENT_MOUSE_LEFT) return INPUT_IGNORE;
	state->object = OBJECT_GARRISON;
	return 0;
}

static int input_object_center(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state *restrict state = argument;
	if (code != EVENT_MOUSE_LEFT) return INPUT_IGNORE;
	state->object = OBJECT_CENTER;
	return 0;
}

static void if_regions(const void *restrict argument, const struct game *restrict game)
{
	const struct state *state = argument;

	size_t i;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	image_draw(&image_world, EDITOR_X, EDITOR_Y);

	// Fill regions with colors.
	for(i = 0; i < game->regions_count; ++i)
		fill_polygon(game->regions[i].location, EDITOR_X, EDITOR_Y, display_colors[Player + state->colors[i]]);

	// Draw region borders.
	for(i = 0; i < game->regions_count; ++i)
		draw_polygon(game->regions[i].location, EDITOR_X, EDITOR_Y, display_colors[Black]);

	// Draw region garrison and center.
	for(i = 0; i < game->regions_count; ++i)
	{
		struct point location = game->regions[i].location_garrison;
		location.x += EDITOR_X - image_garrison.width / 2;
		location.y += EDITOR_Y - image_garrison.height / 2;
		display_image(&image_garrison, location.x, location.y, image_garrison.width, image_garrison.height);

		fill_rectangle(game->regions[i].center.x - 2, game->regions[i].center.y - 2, CENTER_WIDTH, CENTER_HEIGHT, display_colors[Black]);
	}

	if (state->region_index >= 0)
	{
		fill_rectangle(REGIONNAME_X - 2, REGIONNAME_Y - 2, 244, 24, display_colors[Black]);

		if (state->name_size)
			draw_string(state->name, state->name_size, REGIONNAME_X, REGIONNAME_Y + MARGIN, &font12, White);
		draw_cursor(state->name, state->name_position, REGIONNAME_X, REGIONNAME_Y + MARGIN, &font12, White);
	}

	show_button(S("Points tool"), BUTTON_POINTS_X, BUTTON_POINTS_Y);

	switch (state->object)
	{
	case OBJECT_GARRISON:
		display_image(&image_garrison, OBJECT_X, OBJECT_Y, image_garrison.width, image_garrison.height);
		display_image(&image_village_gray, OBJECT_X + 48, OBJECT_Y, image_village_gray.width, image_village_gray.height);
		break;

	case OBJECT_CENTER:
		display_image(&image_garrison_gray, OBJECT_X, OBJECT_Y, image_garrison_gray.width, image_garrison_gray.height);
		display_image(&image_village, OBJECT_X + 48, OBJECT_Y, image_village.width, image_village.height);
		break;
	}

	glFlush();
	glXSwapBuffers(display, drawable);
}

static void draw_line(struct point start, struct point end)
{
	glBegin(GL_LINES);
	glVertex2f(start.x, start.y);
	glVertex2f(end.x, end.y);
	glEnd();
}

static void if_points(const void *argument, const struct game *game)
{
	const struct state *state = argument;

	size_t i;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	image_draw(&image_world, EDITOR_X, EDITOR_Y);

	// Draw the contours of the regions.
	if (state->points.count)
	{
		const struct vertex *restrict points = state->points.data;
		size_t region_first = 0;

		glColor3ub(0, 0, 0);
		for(i = 1; 1; ++i)
		{
			if ((i == state->points.count) || (points[i].region != points[region_first].region)) // last vertex of the region
			{
				if (i <= state->index_start)
					draw_line(points[i - 1].point, points[region_first].point);

				if (i == state->points.count) break;

				region_first = i;
				continue;
			}

			draw_line(points[i - 1].point, points[i].point);
		}
	}

	// Display regions vertices.
	for(i = 0; i < state->index_start; ++i)
	{
		struct point point = state->points.data[i].point;
		draw_rectangle(point.x - 3, point.y - 3, 7, 7, display_colors[Black]);
		fill_rectangle(point.x - 2, point.y - 2, 5, 5, display_colors[Ally]);
	}
	for(; i < state->points.count; ++i)
	{
		struct point point = state->points.data[i].point;
		draw_rectangle(point.x - 3, point.y - 3, 7, 7, display_colors[Black]);
		fill_rectangle(point.x - 2, point.y - 2, 5, 5, display_colors[Enemy]);
	}

	show_button(S("Regions tool"), BUTTON_REGIONS_X, BUTTON_REGIONS_Y);

	glFlush();
	glXSwapBuffers(display, drawable);
}

static int input_regions(const struct game *restrict game, struct state *restrict state)
{
	extern unsigned SCREEN_WIDTH, SCREEN_HEIGHT;

	struct area areas[] = {
		{
			.left = 0,
			.right = SCREEN_WIDTH - 1,
			.top = 0,
			.bottom = SCREEN_HEIGHT - 1,
			.callback = input_region
		},
		{
			.left = BUTTON_POINTS_X,
			.right = BUTTON_POINTS_X + BUTTON_WIDTH - 1,
			.top = BUTTON_POINTS_Y,
			.bottom = BUTTON_POINTS_Y + BUTTON_HEIGHT - 1,
			.callback = input_tool_points
		},
		{
			.left = OBJECT_X,
			.right = OBJECT_X + image_garrison.width - 1,
			.top = OBJECT_Y,
			.bottom = OBJECT_Y + image_garrison.height - 1,
			.callback = input_object_garrison
		},
		{
			.left = OBJECT_X + 48,
			.right = OBJECT_X + 48 + image_village.width - 1,
			.top = OBJECT_Y,
			.bottom = OBJECT_Y + image_village.height - 1,
			.callback = input_object_center
		},
	};

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_regions, game, state);
}

static int input_points(const struct game *restrict game, struct state *restrict state)
{
	extern unsigned SCREEN_WIDTH, SCREEN_HEIGHT;

	struct area areas[] = {
		{
			.left = 0,
			.right = SCREEN_WIDTH - 1,
			.top = 0,
			.bottom = SCREEN_HEIGHT - 1,
			.callback = input_point
		},
		{
			.left = BUTTON_REGIONS_X,
			.right = BUTTON_REGIONS_X + BUTTON_WIDTH - 1,
			.top = BUTTON_REGIONS_Y,
			.bottom = BUTTON_REGIONS_Y + BUTTON_HEIGHT - 1,
			.callback = input_tool_regions
		}
	};

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_points, game, state);
}

static int world_init(struct game *restrict game)
{
	game->players_count = 1;
	game->players = malloc(1 * sizeof(*game->players));
	if (!game->players) return ERROR_MEMORY;
	game->players[0].type = Neutral;
	game->players[0].treasury = (struct resources){0};
	game->players[0].alliance = 0;

	game->regions_count = 0;
	game->regions = malloc(1 * sizeof(*game->regions));
	if (!game->regions)
	{
		free(game->players);
		return ERROR_MEMORY;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct game game;
	struct state state = {0};
	int status;

	if (argc < 2)
	{
		write(2, S("Usage: editor <image> [world]\n"));
		return 0;
	}

	write(1, S("WARNING: Vertices in a region must be listed counterclockwise.\n"));
	write(1, S("WARNING: Each region can have up to " LOG_STRING(NEIGHBORS_LIMIT) " neighbors.\n"));

	if_init();

	image_load_png(&image_world, argv[1], 0);
	image_load_png(&image_garrison, PREFIX_IMG "map_fortress.png", 0);
	image_load_png(&image_garrison_gray, PREFIX_IMG "map_fortress.png", image_grayscale);
	image_load_png(&image_village, PREFIX_IMG "map_village.png", 0);
	image_load_png(&image_village_gray, PREFIX_IMG "map_village.png", image_grayscale);

	if_storage_init(image_world.width, image_world.height);

	if_display();

	if (argc > 2) status = world_load(argv[2], &game);
	else status = world_init(&game);
	if (status) return status;

	tool_regions_init(&game, &state);

	while (1)
	{
		switch (editor_tool)
		{
		case TOOL_REGIONS:
			status = input_regions(&game, &state);
			break;

		case TOOL_POINTS:
			status = input_points(&game, &state);
			break;
		}

		if (status < 0)
		{
			if (status == ERROR_CANCEL)
			{
				status = world_save(&game, WORLD_TEMP);
				if (status < 0)
				{
					LOG_ERROR("Error saving world!");
					continue;
				}
			}
			break;
		}
	}

	world_unload(&game);
	write(1, S("world written to " WORLD_TEMP "\n"));

	if_storage_term();
	image_unload(&image_world);
	if_term();

	return 0;
}
