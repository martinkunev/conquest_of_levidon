struct game;

#define GL_GLEXT_PROTOTYPES

#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <GL/glx.h>
#include <GL/glext.h>

#include "errors.h"
#include "format.h"
#include "json.h"
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

// TODO use world_load() and world_save() in the map editor
// TODO tools for: garrison placement
// TODO determine region neighbors automatically

// WARNING: Vertices in a region must be listed counterclockwise.

#define array_name array_vertex
#define array_type struct vertex
#include "generic/array.g"

enum {ARRAY_SIZE_DEFAULT = 16};

#define S(s) (s), sizeof(s) - 1

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
	struct array_vertex points;
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
		return INPUT_FINISH;

	case EVENT_MOUSE_LEFT:
		{
			int index = if_storage_get(x, y);
			if (index > (int)state->region_start) // this point is already added to the current region
				return INPUT_IGNORE;

			if (index >= 0)
			{
				x = state->points.data[index].point.x;
				y = state->points.data[index].point.y;
			}

			if (index == (int)state->region_start)
			{
				if (state->region_start > state->points.count - 3)
					return INPUT_IGNORE;

				if (array_vertex_expand(&state->points, state->points.count + 1) < 0)
					abort();
				state->points.data[state->points.count++] = (struct vertex){x, y, index, state->region};

				state->region_start = state->points.count;
				state->region += 1;
				return 0;
			}

			if_storage_point(x, y, state->points.count);
			if (array_vertex_expand(&state->points, state->points.count + 1) < 0)
				abort();
			state->points.data[state->points.count++] = (struct vertex){x, y, index, state->region};
		}
		return 0;


	case ((255 << 8) | 27): // escape
		if (state->points.count <= state->region_start)
			return INPUT_IGNORE;
		{
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
			if (index < state->region_start) return INPUT_IGNORE;

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
		fill_rectangle(point.x - 2, point.y - 2, 5, 5, Ally);
	}
	for(; i < state->points.count; ++i)
	{
		struct point point = state->points.data[i].point;
		draw_rectangle(point.x - 3, point.y - 3, 7, 7, Black);
		fill_rectangle(point.x - 2, point.y - 2, 5, 5, Enemy);
	}

	glFlush();
	glXSwapBuffers(display, drawable);
}

static void save(const struct array_vertex *points)
{
	size_t i;

	union json *json, *regions, *location, *point;

	json = json_object();
	regions = json_object();
	json = json_object_insert(json, S("regions"), regions);

	unsigned region_start = 0;
	int region = -1;
	for(i = 0; i < points->count; ++i)
	{
		if (points->data[i].region != region)
		{
			char buffer[5], *end; // TODO make sure this is big enough

			region = points->data[i].region;
			region_start = i;

			location = json_array();
			end = format_uint(buffer, region, 10);
			regions = json_object_insert(regions, buffer, end - buffer, json_object_insert(json_object(), S("location"), location));

			continue;
		}

		struct point p = points->data[i - 1].point;

		point = json_array();
		location = json_array_insert(location, point);
		point = json_array_insert(point, json_integer(p.x));
		point = json_array_insert(point, json_integer(p.y));
	}

	if (!json) abort();

	size_t size = json_size(json);
	char *buffer = malloc(size + 1);
	if (!buffer) abort();
	json_dump(buffer, json);
	buffer[size] = '\n';
	write(1, buffer, size + 1);
	free(buffer);
}

static inline union json *value_get_try(const struct hashmap *restrict hashmap, const unsigned char *restrict key, size_t size, enum json_type type)
{
	union json **entry = hashmap_get(hashmap, key, size);
	if (!entry || (json_type(*entry) != type)) return 0;
	return *entry;
}

static void load_point(const union json *point, size_t region, struct array_vertex *restrict points)
{
	if ((json_type(point) != JSON_ARRAY) || (point->array.count != 2)) abort();
	if (json_type(point->array.data[0]) != JSON_INTEGER) abort();
	if (json_type(point->array.data[1]) != JSON_INTEGER) abort();

	unsigned x = point->array.data[0]->integer;
	unsigned y = point->array.data[1]->integer;

	int index = if_storage_get(x, y);
	/*if (index >= (int)region_start) // this point is already added to the current region
		abort();*/

	if (index >= 0)
	{
		x = points->data[index].point.x;
		y = points->data[index].point.y;
	}

	if_storage_point(x, y, points->count);
	if (array_vertex_expand(points, points->count + 1) < 0)
		abort();
	points->data[points->count++] = (struct vertex){x, y, index, region};
}

static void load(const unsigned char *restrict buffer, size_t size, struct array_vertex *restrict points)
{
	size_t region_index, p;

	union json *json, *regions;

	struct hashmap_iterator it;
	struct hashmap_entry *entry;

	if (array_vertex_init(points, ARRAY_SIZE_DEFAULT) < 0)
		abort();

	json = json_parse(buffer, size);
	if (!json || (json_type(json) != JSON_OBJECT)) abort();
	regions = value_get_try(&json->object, S("regions"), JSON_OBJECT);
	if (!regions) abort();

	region_index = 0;
	for(entry = hashmap_first(&regions->object, &it); entry; entry = hashmap_next(&regions->object, &it))
	{
		union json *region, *location;

		region = entry->value;
		if (json_type(region) != JSON_OBJECT) abort();

		location = value_get_try(&region->object, S("location"), JSON_ARRAY);
		if (!location) abort();

		for(p = 0; p < location->array.count; ++p)
			load_point(location->array.data[p], region_index, points);
		load_point(location->array.data[0], region_index, points);

		region_index += 1;
	}

	json_free(json);
}

static void input(struct state *restrict state)
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

	input_local(areas, sizeof(areas) / sizeof(*areas), if_editor, 0, state);
}

int main(int argc, char *argv[])
{
	struct state state;

	if_init();
	if (argc > 1) image_load_png(&image_world, argv[1], 0);
	if_storage_init();

	if_display();

	if (argc > 2)
	{
		struct stat info;
		unsigned char *buffer;

		int world = open(argv[2], O_RDONLY);
		if (world < 0) abort();
		if (fstat(world, &info) < 0)
		{
			close(world);
			return 0;
		}
		buffer = mmap(0, info.st_size, PROT_READ, MAP_PRIVATE, world, 0);
		close(world);
		if (buffer == MAP_FAILED) abort();

		load(buffer, info.st_size, &state.points);
		if (!state.points.count) abort();
		state.region_start = state.points.count;
		state.region = state.points.data[state.points.count - 1].region + 1;

		munmap(buffer, info.st_size);
	}
	else
	{
		if (array_vertex_init(&state.points, ARRAY_SIZE_DEFAULT) < 0)
			abort();
		state.region_start = state.points.count;
		state.region = 0;
	}

	input(&state);

	save(&state.points);
	array_vertex_term(&state.points);

	if_storage_term();

	if_term();

	return 0;
}
