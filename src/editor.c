#define GL_GLEXT_PROTOTYPES

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
#include "map.h"
#include "input.h"
#include "image.h"
#include "interface.h"
#include "pathfinding.h"
#include "display_common.h"
#include "world.h"

#define EDITOR_X 0
#define EDITOR_Y 0

#define BUTTON_TOOL_X 900
#define BUTTON_TOOL_Y 16

#define REGIONNAME_X 800
#define REGIONNAME_Y 64

#define WORLD_TEMP "/tmp/conquest_of_levidon_world"

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

// TODO support deleting a region, setting region garrison and setting region center

#define array_name array_vertex
#define array_type struct vertex
#include "generic/array.g"

static GLuint map_framebuffer;
static GLuint map_renderbuffer;

struct state
{
	// TOOL_REGIONS
	unsigned char *colors;
	size_t region_index;
	char name[NAME_LIMIT];
	size_t name_size;
	size_t name_position;

	// TOOL_POINTS
	struct array_vertex points;
	size_t index_start;
};

static struct image image_world;

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
	size_t i;

	memset(region->name, 0, sizeof(region->name));
	region->name_length = 0;

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
	region->construct = 0;
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

	LOG(LOG_ERROR, "Region %u has more than %u neighbors.", (unsigned)region->index, (unsigned)NEIGHBORS_LIMIT);
}

static void tool_points_term(struct game *restrict game, struct state *restrict state)
{
	if (state->points.count > state->index_start) // a new region was just added
	{
		struct region *restrict region, *restrict neighbor;
		size_t i;

		void *buffer = realloc(game->regions, (game->regions_count + 1) * sizeof(*game->regions));
		if (!buffer) abort();
		game->regions = buffer;

		region = game->regions + game->regions_count;
		region_init(game, region, state->points.data + state->index_start, state->points.count - state->index_start);

		// Determine the neighbors of the region.
		for(i = 0; i < game->regions_count; ++i)
		{
			neighbor = game->regions + i;
			if (polygons_border(region->location, neighbor->location, 0, 0))
			{
				add_neighbor(region, neighbor);
				add_neighbor(neighbor, region);
			}
		}

		game->regions_count += 1;
	}

	free(state->points.data);
}

/*
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
*/

static int input_region(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state *state = argument;

	switch (code)
	{
	case XK_Escape:
		// Exit the editor.
		return INPUT_TERMINATE;

	case EVENT_MOUSE_LEFT:
		{
			int index = if_storage_get(x, y);
			if (index < 0) return INPUT_IGNORE;

			state->region_index = index;
			state->name_size = 0;
			state->name_position = 0;
		}
		return 0;

	case EVENT_MOUSE_RIGHT:
		{
			int index = if_storage_get(x, y);
			if (index < 0) return INPUT_IGNORE;

			// TODO remove the region
		}
		return 0;

	default:
		if (state->region_index < 0) return INPUT_IGNORE;

		if ((code > 31) && (code < 127))
		{
			if (state->name_size == NAME_LIMIT) return INPUT_IGNORE; // TODO indicate that the buffer is full

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

	case EVENT_MOUSE_LEFT:
		{
			int index = if_storage_get(x, y);
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

	case XK_BackSpace:
		{
			if (state->points.count <= state->index_start)
				return INPUT_IGNORE;

			struct vertex v = state->points.data[state->points.count - 1];
			if_storage_point(v.point.x, v.point.y, v.previous);
			state->points.count -= 1;
		}
		return 0;

	case EVENT_MOUSE_RIGHT:
		{
			size_t i;

			int index = if_storage_get(x, y);
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

static void if_regions(const void *restrict argument, const struct game *restrict game)
{
	const struct state *state = argument;

	size_t i;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	image_draw(&image_world, EDITOR_X, EDITOR_Y);

	// Fill regions with colors.
	for(i = 0; i < game->regions_count; ++i)
		fill_polygon(game->regions[i].location, 0, 0, display_colors[Players + state->colors[i]]);

	// Draw region borders.
	for(i = 0; i < game->regions_count; ++i)
		draw_polygon(game->regions[i].location, EDITOR_X, EDITOR_Y, display_colors[Black]);

	if (state->name_size)
		draw_string(state->name, state->name_size, REGIONNAME_X, REGIONNAME_Y + MARGIN, &font12, White);
	draw_cursor(state->name, state->name_position, REGIONNAME_X, REGIONNAME_Y + MARGIN, &font12, White);

	show_button(S("Points tool"), BUTTON_TOOL_X, BUTTON_TOOL_Y);

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
				draw_line(points[i].point, points[region_first].point);

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
		draw_rectangle(point.x - 3, point.y - 3, 7, 7, Black);
		fill_rectangle(point.x - 2, point.y - 2, 5, 5, Ally);
	}
	for(; i < state->points.count; ++i)
	{
		struct point point = state->points.data[i].point;
		draw_rectangle(point.x - 3, point.y - 3, 7, 7, Black);
		fill_rectangle(point.x - 2, point.y - 2, 5, 5, Enemy);
	}

	show_button(S("Regions tool"), BUTTON_TOOL_X, BUTTON_TOOL_Y);

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
			.left = BUTTON_TOOL_X,
			.right = BUTTON_TOOL_X + BUTTON_WIDTH,
			.top = BUTTON_TOOL_Y,
			.bottom = BUTTON_TOOL_Y + BUTTON_HEIGHT,
			.callback = input_tool_points
		}
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
			.left = BUTTON_TOOL_X,
			.right = BUTTON_TOOL_X + BUTTON_WIDTH,
			.top = BUTTON_TOOL_Y,
			.bottom = BUTTON_TOOL_Y + BUTTON_HEIGHT,
			.callback = input_tool_regions
		}
	};

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_points, game, state);
}

static int world_init(struct game *restrict game)
{
	game->players_count = 0;
	game->players = malloc(1 * sizeof(*game->players));
	if (!game->players) return ERROR_MEMORY;

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
	if_storage_init(image_world.width, image_world.height);

	if_display();

	if (argc > 2) status = world_load(argv[2], &game);
	else status = world_init(&game);
	if (status) return status;

	tool_regions_init(&game, &state);

	do
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
	} while (!status);

	world_save(&game, WORLD_TEMP);
	write(1, S("world written to " WORLD_TEMP "\n"));

	if_storage_term();
	image_unload(&image_world);
	if_term();

	return 0;
}
