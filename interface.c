#include <stdlib.h>
#include <sys/time.h>
#include <math.h>

#define GL_GLEXT_PROTOTYPES

//#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glext.h>

#include <xcb/xcb.h>

//#include <X11/Xlib-xcb.h>

#include "types.h"
#include "format.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "movement.h"
#include "image.h"
#include "input.h"
#include "input_map.h"
#include "input_battle.h"
#include "interface.h"

// http://xcb.freedesktop.org/opengl/
// http://xcb.freedesktop.org/tutorial/events/
// http://techpubs.sgi.com/library/dynaweb_docs/0640/SGI_Developer/books/OpenGL_Porting/sgi_html/ch04.html
// http://tronche.com/gui/x/xlib/graphics/font-metrics/
// http://open.gl
// http://www.opengl.org/sdk/docs/man2/

// Use xlsfonts to list the available fonts.
// "-misc-dejavu sans mono-bold-r-normal--12-0-0-0-m-0-ascii-0"

#define S(s) (s), sizeof(s) - 1

#define RESOURCE_GOLD 660
#define RESOURCE_FOOD 680
#define RESOURCE_WOOD 700
#define RESOURCE_IRON 720
#define RESOURCE_STONE 740

// TODO compatibility with OpenGL 2.1 (used in MacOS X)
#define glGenFramebuffers(...) glGenFramebuffersEXT(__VA_ARGS__)
#define glGenRenderbuffers(...) glGenRenderbuffersEXT(__VA_ARGS__)
#define glBindFramebuffer(...) glBindFramebufferEXT(__VA_ARGS__)
#define glBindRenderbuffer(...) glBindRenderbufferEXT(__VA_ARGS__)
#define glRenderbufferStorage(...) glRenderbufferStorageEXT(__VA_ARGS__)
#define glFramebufferRenderbuffer(...) glFramebufferRenderbufferEXT(__VA_ARGS__)

#define ANIMATION_DURATION 4.0

#define WM_STATE "_NET_WM_STATE"
#define WM_STATE_FULLSCREEN "_NET_WM_STATE_FULLSCREEN"

static Display *display;
static xcb_window_t window;
static GLXDrawable drawable;
static GLXContext context;
static xcb_screen_t *screen;

xcb_connection_t *connection;
KeySym *keymap;
int keysyms_per_keycode;
int keycode_min, keycode_max;
GLuint map_framebuffer;

// TODO Create a struct that stores all the information about the battle (battlefield, players, etc.)
struct battle *battle;
struct battlefield (*battlefield)[BATTLEFIELD_WIDTH];

struct region *restrict regions;
size_t regions_count;

static struct image image_move_destination, image_shoot_destination, image_selected, image_flag, image_panel, image_construction;
static struct image image_gold, image_food, image_wood, image_stone, image_iron, image_time;
static struct image image_scroll_left, image_scroll_right;
static struct image image_units[4]; // TODO the array must be enough to hold units_count units
static struct image image_buildings[8]; // TODO the array must be big enough to hold buildings_count elements
static struct image image_buildings_gray[8]; // TODO the array must be big enough to hold buildings_count elements

static GLuint map_renderbuffer;

struct font font12;

uint8_t *format_sint(uint8_t *buffer, int64_t number)
{
	if (number > 0) *buffer++ = '+';
	return format_int(buffer, number, 10);
}

static void if_reshape(int width, int height)
{
	glViewport(0, 0, width, height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, width, height, 0, 0, 1);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

static xcb_intern_atom_reply_t *request_atom(xcb_connection_t *restrict connection, const char *restrict name, size_t name_size)
{
	xcb_generic_error_t *error;
	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(connection, 0, sizeof(name_size), name);
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookie, &error);
	if (error) return 0;
	return reply;
}

int if_init(void)
{
	display = XOpenDisplay(0);
	if (!display) return -1;

	int default_screen = XDefaultScreen(display);

	connection = XGetXCBConnection(display);
	if (!connection) goto error;

	int visualID = 0;

	XSetEventQueueOwner(display, XCBOwnsEventQueue);

	// find XCB screen
	xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(xcb_get_setup(connection));
	int screen_num = default_screen;
	while (screen_iter.rem && screen_num > 0)
	{
		screen_num -= 1;
		xcb_screen_next(&screen_iter);
	}
	screen = screen_iter.data;

	// query framebuffer configurations
	GLXFBConfig *fb_configs = 0;
	int num_fb_configs = 0;
	fb_configs = glXGetFBConfigs(display, default_screen, &num_fb_configs);
	if (!fb_configs || num_fb_configs == 0) goto error;

	// select first framebuffer config and query visualID
	GLXFBConfig fb_config = fb_configs[0];
	glXGetFBConfigAttrib(display, fb_config, GLX_VISUAL_ID , &visualID);

	// create OpenGL context
	context = glXCreateNewContext(display, fb_config, GLX_RGBA_TYPE, 0, True);
	if (!context) goto error;

	// create XID's for colormap and window
	xcb_colormap_t colormap = xcb_generate_id(connection);
	window = xcb_generate_id(connection);

	xcb_create_colormap(connection, XCB_COLORMAP_ALLOC_NONE, colormap, screen->root, visualID);

	uint32_t eventmask = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION;
	uint32_t valuelist[] = {eventmask, colormap, 0};
	uint32_t valuemask = XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;

	// TODO set window parameters
	xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 100, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, visualID, valuemask, valuelist);

	// NOTE: window must be mapped before glXMakeContextCurrent
	xcb_map_window(connection, window); 

	drawable = glXCreateWindow(display, fb_config, window, 0);

	if (!window)
	{
		xcb_destroy_window(connection, window);
		glXDestroyContext(display, context);
		goto error;
	}

	// make OpenGL context current
	if (!glXMakeContextCurrent(display, drawable, drawable, context))
	{
		xcb_destroy_window(connection, window);
		glXDestroyContext(display, context);
		goto error;
	}

	// TODO use DPI-based font size
	// https://wiki.archlinux.org/index.php/X_Logical_Font_Description
	if (font_init(display, &font12, "-misc-dejavu sans-bold-r-normal--12-0-0-0-p-0-ascii-0") < 0)
	{
		xcb_destroy_window(connection, window);
		glXDestroyContext(display, context);
		goto error;
	}

	// enable transparency
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// TODO set window title, border, etc.

	if_reshape(SCREEN_WIDTH, SCREEN_HEIGHT); // TODO call this after resize

//#define _NET_WM_STATE_REMOVE        0    // remove/unset property
//#define _NET_WM_STATE_ADD           1    // add/set property
//#define _NET_WM_STATE_TOGGLE        2    // toggle property
	// Make the window fullscreen.
	{
		xcb_intern_atom_reply_t *reply_state = request_atom(connection, WM_STATE, sizeof(WM_STATE));
		if (!reply_state)
			goto error; // TODO

		xcb_intern_atom_reply_t *reply_fullscreen = request_atom(connection, WM_STATE_FULLSCREEN, sizeof(WM_STATE_FULLSCREEN));
		if (!reply_fullscreen)
			goto error; // TODO

		xcb_client_message_event_t event;
		memset(&event, 0, sizeof(event));

		event.response_type = XCB_CLIENT_MESSAGE;
		event.format = 32;
		event.window = window;
		event.type = reply_state->atom;

		event.data.data32[0] = 1; // fullscreen on
		event.data.data32[1] = reply_fullscreen->atom;
		event.data.data32[2] = XCB_ATOM_NONE;

		xcb_send_event(connection, 1, window, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY, (const char *)&event);
	}

	image_load_png(&image_move_destination, "img/move_destination.png", 0);
	image_load_png(&image_shoot_destination, "img/shoot_destination.png", 0);
	image_load_png(&image_selected, "img/selected.png", 0);
	image_load_png(&image_flag, "img/flag.png", 0);
	image_load_png(&image_panel, "img/panel.png", 0);
	image_load_png(&image_construction, "img/construction.png", 0);

	image_load_png(&image_scroll_left, "img/scroll_left.png", 0);
	image_load_png(&image_scroll_right, "img/scroll_right.png", 0);

	image_load_png(&image_gold, "img/gold.png", 0);
	image_load_png(&image_food, "img/food.png", 0);
	image_load_png(&image_wood, "img/wood.png", 0);
	image_load_png(&image_stone, "img/stone.png", 0);
	image_load_png(&image_iron, "img/iron.png", 0);
	image_load_png(&image_time, "img/time.png", 0);

	image_load_png(&image_units[0], "img/peasant.png", 0);
	image_load_png(&image_units[1], "img/archer.png", 0);
	image_load_png(&image_units[2], "img/pikeman.png", 0);
	image_load_png(&image_units[3], "img/horse_rider.png", 0);

	image_load_png(&image_buildings[0], "img/irrigation.png", 0);
	image_load_png(&image_buildings[1], "img/lumbermill.png", 0);
	image_load_png(&image_buildings[2], "img/mine.png", 0);
	image_load_png(&image_buildings[3], "img/blast_furnace.png", 0);
	image_load_png(&image_buildings[4], "img/barracks.png", 0);
	image_load_png(&image_buildings[5], "img/archery_range.png", 0);
	image_load_png(&image_buildings[6], "img/stables.png", 0);
	image_load_png(&image_buildings[7], "img/watch_tower.png", 0);

	image_load_png(&image_buildings_gray[0], "img/irrigation.png", 1);
	image_load_png(&image_buildings_gray[1], "img/lumbermill.png", 1);
	image_load_png(&image_buildings_gray[2], "img/mine.png", 1);
	image_load_png(&image_buildings_gray[3], "img/blast_furnace.png", 1);
	image_load_png(&image_buildings_gray[4], "img/barracks.png", 1);
	image_load_png(&image_buildings_gray[5], "img/archery_range.png", 1);
	image_load_png(&image_buildings_gray[6], "img/stables.png", 1);
	image_load_png(&image_buildings_gray[7], "img/watch_tower.png", 1);

	// TODO handle modifier keys
	// TODO handle dead keys
	// TODO the keyboard mappings don't work as expected for different keyboard layouts

	// Initialize keyboard mapping table.
	XDisplayKeycodes(display, &keycode_min, &keycode_max);
	keymap = XGetKeyboardMapping(display, keycode_min, (keycode_max - keycode_min + 1), &keysyms_per_keycode);
	if (!keymap) return -1; // TODO error

	glGenFramebuffers(1, &map_framebuffer);
	glGenRenderbuffers(1, &map_renderbuffer);

	glBindFramebuffer(GL_FRAMEBUFFER, map_framebuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, map_renderbuffer);

	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB8, MAP_WIDTH, MAP_HEIGHT);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, map_renderbuffer);

	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	return 0;

error:
	// TODO free what there is to be freed
	XCloseDisplay(display);
	return -1;
}

static void display_unit(size_t unit, unsigned x, unsigned y, enum color color, enum color text, unsigned count)
{
	display_rectangle(x, y, FIELD_SIZE, FIELD_SIZE, color);
	image_draw(&image_units[unit], x, y);

	if (count)
	{
		char buffer[16];
		size_t length = format_uint(buffer, count, 10) - (uint8_t *)buffer;
		display_string(buffer, length, x + (FIELD_SIZE - (length * 10)) / 2, y + FIELD_SIZE, &font12, text);
	}
}

static void show_progress(unsigned current, unsigned total, unsigned x, unsigned y, unsigned width, unsigned height)
{
	// Progress is visualized as a sector with length proportional to the remaining time.

	if (current)
	{
		double progress = (double)current / total;
		double angle = progress * 2 * M_PI;

		double cx = x + width / 2, cy = y + height / 2;

		glColor4ubv(display_colors[Progress]);

		glBegin(GL_POLYGON);

		glVertex2f(x + width / 2, y + height / 2);
		switch ((unsigned)(progress * 8))
		{
		case 0:
		case 7:
			glVertex2f(x + width, y + (1 - sin(angle)) * height / 2);
			break;

		case 1:
		case 2:
			glVertex2f(x + (1 + cos(angle)) * width / 2, y);
			break;

		case 3:
		case 4:
			glVertex2f(x, y + (1 - sin(angle)) * height / 2);
			break;

		case 5:
		case 6:
			glVertex2f(x + (1 + cos(angle)) * width / 2, y + height);
			break;
		}
		switch ((unsigned)(progress * 8))
		{
		case 0:
			glVertex2f(x + width, y);
		case 1:
		case 2:
			glVertex2f(x, y);
		case 3:
		case 4:
			glVertex2f(x, y + height);
		case 5:
		case 6:
			glVertex2f(x + width, y + height);
		}
		glVertex2f(x + width, y + height / 2);

		glEnd();
	}
	else display_rectangle(x, y, width, height, Progress);
}

/*#include <stdarg.h>
#include <stdio.h>

static struct polygon *region_create(size_t count, ...)
{
	size_t index;
	va_list vertices;

	// Allocate memory for the region and its vertices.
	struct polygon *polygon = malloc(sizeof(struct polygon) + count * sizeof(struct point));
	if (!polygon) return 0;
	polygon->vertices_count = count;

	// Initialize region vertices.
	va_start(vertices, count);
	for(index = 0; index < count; ++index)
		polygon->points[index] = va_arg(vertices, struct point);
	va_end(vertices);

	return polygon;
}*/

/*
void if_test(const struct player *restrict players, const struct state *restrict state, const struct game *restrict game)
{
	// clear window
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	size_t i, j;

	for(i = 0; i < BATTLEFIELD_HEIGHT; ++i)
		for(j = 0; j < BATTLEFIELD_WIDTH; ++j)
			if ((i + j) % 2)
				display_rectangle(j * FIELD_SIZE, i * FIELD_SIZE, FIELD_SIZE, FIELD_SIZE, B0);

	/////////

	static size_t obstacles_count = 1;
	static struct polygon *obstacles = 0;
	static struct vector_adjacency nodes;

	static struct point from = {0, 0};
	static struct point to = {BATTLEFIELD_WIDTH, BATTLEFIELD_HEIGHT};
	static struct queue moves = {0};

	if ((state->x != to.x) || (state->y != to.y))
	{
		to.x = state->x;
		to.y = state->y;

		if (!obstacles)
		{
			//obstacles = region_create(5, (struct point){13, 13}, (struct point){13, 9}, (struct point){6, 9}, (struct point){6, 13}, (struct point){13, 13});
			obstacles = region_create(4, (struct point){13, 13}, (struct point){13, 9}, (struct point){6, 9}, (struct point){6, 13});
			visibility_graph_build(obstacles, obstacles_count, &nodes);
		}

		queue_term_free(&moves, free);
		queue_init(&moves);

		struct move m;
		m.location = from;
		m.time = 0;
		m.distance = 0;
		queue_push(&moves, m);

		if (path_find(&moves, to, &nodes, obstacles, obstacles_count) < 0)
		{
			//
		}

		/ *
		visibility_graph_free(&nodes);
		free(obstacles);
		* /
	}

	if ((state->x != BATTLEFIELD_WIDTH) && (state->y != BATTLEFIELD_HEIGHT))
	{
		if (moves.length > 1)
		{
			/ *obstacles->points[0].x += 1;
			obstacles->points[0].y += 1;
			obstacles->points[1].x += 1;
			obstacles->points[3].y += 1;
			obstacles->vertices_count -= 1;
			for(i = 0; i < obstacles->vertices_count; ++i)
			{
				obstacles->points[i].x *= FIELD_SIZE;
				obstacles->points[i].y *= FIELD_SIZE;
			}

			glColor4ubv(display_colors[Enemy]);
			display_polygon(obstacles, BATTLE_X, BATTLE_Y);

			for(i = 0; i < obstacles->vertices_count; ++i)
			{
				obstacles->points[i].x /= FIELD_SIZE;
				obstacles->points[i].y /= FIELD_SIZE;
			}
			obstacles->vertices_count += 1;
			obstacles->points[0].x -= 1;
			obstacles->points[0].y -= 1;
			obstacles->points[1].x -= 1;
			obstacles->points[3].y -= 1;* /

			glColor4ubv(display_colors[Enemy]);
			glBegin(GL_LINE_STRIP);
			for(i = 0; i < obstacles->vertices_count; ++i)
				glVertex2i(obstacles->points[i].x * FIELD_SIZE + 16, obstacles->points[i].y * FIELD_SIZE + 16);
			glEnd();

			struct queue_item *m;
			for(m = moves.first; m->next; m = m->next)
			{
				struct point from = m->data.location;
				from.x = from.x * FIELD_SIZE + FIELD_SIZE / 2;
				from.y = from.y * FIELD_SIZE + FIELD_SIZE / 2;

				struct point to = m->next->data.location;;
				to.x = to.x * FIELD_SIZE + FIELD_SIZE / 2;
				to.y = to.y * FIELD_SIZE + FIELD_SIZE / 2;

				display_arrow(from, to, BATTLE_X, BATTLE_Y, Self);
			}
		}
	}

	/////////

	glFlush();
	glXSwapBuffers(display, drawable);
}*/

// TODO write this better
static int if_animation(const struct player *restrict players, const struct battle *restrict battle, double progress)
{
	int finished = 1;

	// TODO in some cases the animation must terminate but it doesn't

	// clear window
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Battlefield

	display_rectangle(0, 0, BATTLEFIELD_WIDTH * FIELD_SIZE, BATTLEFIELD_HEIGHT * FIELD_SIZE, B0);

	struct point location;
	size_t p;
	for(p = 0; p < battle->pawns_count; ++p)
	{
		struct pawn *pawn = battle->pawns + p;
		double x, y;

		if (!pawn->slot->count) continue;

		// TODO improve this check
		if (movement_location(pawn, progress, &x, &y) < pawn->moves_count)
			finished = 0;

		display_rectangle(x * FIELD_SIZE, y * FIELD_SIZE, FIELD_SIZE, FIELD_SIZE, Player + pawn->slot->player);
		image_draw(&image_units[pawn->slot->unit->index], x * FIELD_SIZE, y * FIELD_SIZE);
	}

	glFlush();
	glXSwapBuffers(display, drawable);

	return finished;
}
static inline unsigned long timediff(const struct timeval *restrict end, const struct timeval *restrict start)
{
	return (end->tv_sec * 1000000 + end->tv_usec - start->tv_sec * 1000000 - start->tv_usec);
}
void input_animation(const struct game *restrict game, const struct battle *restrict battle)
{
	struct timeval start, now;
	double progress;
	gettimeofday(&start, 0);
	do
	{
		gettimeofday(&now, 0);
		progress = timediff(&now, &start) / (ANIMATION_DURATION * 1000000.0);
		if (if_animation(game->players, battle, progress))
			break;
	} while (progress < 1.0);
}

void if_formation(const void *argument, const struct game *game)
{
	const struct state_formation *state = argument;

	// clear window
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// show the right panel in gray
	display_rectangle(768, 0, 256, 768, Gray);

	// draw rectangle with current player's color
	display_rectangle(768, 0, 256, 16, Player + state->player);

	// Display hovered field in color.
	if (!point_eq(state->hover, POINT_NONE))
		display_rectangle(state->hover.x * FIELD_SIZE, state->hover.y * FIELD_SIZE, FIELD_SIZE, FIELD_SIZE, Unexplored);

	const struct region *region = game->regions + state->region;

	size_t i;
	const struct vector *pawns = battle->player_pawns + state->player;
	for(i = 0; i < pawns->length; ++i)
	{
		const struct pawn *pawn = pawns->data[i];
		const struct troop *slot = pawn->slot;

		if (pawn == state->pawn)
		{
			// Display the selected pawn in the control panel.
			display_unit(slot->unit->index, CTRL_X, CTRL_Y, Player + state->player, White, slot->count);

			// Display at which fields the pawn can be placed.
			const struct point *positions = formation_positions(slot, region);
			for(i = 0; i < PAWNS_LIMIT; ++i)
				if (!battlefield[positions[i].y][positions[i].x].pawn)
					display_rectangle(positions[i].x * FIELD_SIZE, positions[i].y * FIELD_SIZE, FIELD_SIZE, FIELD_SIZE, Gray);
		}
		else
		{
			// Display the pawn at its present location.
			struct point location = pawn->moves[0].location;
			display_unit(slot->unit->index, location.x * FIELD_SIZE, location.y * FIELD_SIZE, Player + state->player, 0, 0);
		}
	}

	glFlush();
	glXSwapBuffers(display, drawable);
}

void if_battle(const void *argument, const struct game *game)
{
	const struct state_battle *state = argument;

	// clear window
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// show the right panel in gray
	display_rectangle(768, 0, 256, 768, Gray);

	// draw rectangle with current player's color
	display_rectangle(768, 0, 256, 16, Player + state->player);

	size_t x, y;
	const struct pawn *p;

	// Battlefield

	display_rectangle(0, 0, BATTLEFIELD_WIDTH * FIELD_SIZE, BATTLEFIELD_HEIGHT * FIELD_SIZE, B0);

	// Display hovered field in color.
	if (!point_eq(state->hover, POINT_NONE))
		display_rectangle(state->hover.x * FIELD_SIZE, state->hover.y * FIELD_SIZE, FIELD_SIZE, FIELD_SIZE, Unexplored);

	// display pawns
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			if (p = battlefield[y][x].pawn)
			{
				enum color color;
				if (p->slot->player == state->player) color = Self;
				else if (game->players[p->slot->player].alliance == game->players[state->player].alliance) color = Ally;
				else color = Enemy;

				display_rectangle(x * FIELD_SIZE, y * FIELD_SIZE, FIELD_SIZE, FIELD_SIZE, color);
				image_draw(&image_units[p->slot->unit->index], x * FIELD_SIZE, y * FIELD_SIZE);
			}
		}

	unsigned row, column;

	// Display information about the selected field.
	if ((state->x < BATTLEFIELD_WIDTH) && (state->y < BATTLEFIELD_HEIGHT) && battlefield[state->y][state->x].pawn)
	{
		enum color color;

		image_draw(&image_selected, state->x * FIELD_SIZE, state->y * FIELD_SIZE);

		p = battlefield[state->y][state->x].pawn;
		if (p->slot->player == state->player) color = Self;
		else if (game->players[p->slot->player].alliance == game->players[state->player].alliance) color = Ally;
		else color = Enemy;
		display_rectangle(CTRL_X, CTRL_Y, FIELD_SIZE + MARGIN * 2, FIELD_SIZE + font12.height + MARGIN * 2, color);
		display_unit(p->slot->unit->index, CTRL_X + MARGIN, CTRL_Y + MARGIN, Player + p->slot->player, Black, p->slot->count);

		// Show pawn task (if any).
		if (p->slot->player == state->player)
		{
			size_t i;
			for(i = 1; i < p->moves_count; ++i)
			{
				struct point from = p->moves[i - 1].location;
				from.x = from.x * FIELD_SIZE + FIELD_SIZE / 2;
				from.y = from.y * FIELD_SIZE + FIELD_SIZE / 2;

				struct point to = p->moves[i].location;
				to.x = to.x * FIELD_SIZE + FIELD_SIZE / 2;
				to.y = to.y * FIELD_SIZE + FIELD_SIZE / 2;

				if (p->moves[i].time <= 1.0) color = Reachable;
				else if (p->moves[i - 1].time <= 1.0) color = Partial;
				else color = Unreachable;
				display_arrow(from, to, BATTLE_X, BATTLE_Y, color);
			}

			if (!point_eq(p->shoot, POINT_NONE))
				image_draw(&image_shoot_destination, p->shoot.x * FIELD_SIZE, p->shoot.y * FIELD_SIZE);
		}
	}

	glFlush();
	glXSwapBuffers(display, drawable);

	// TODO finish this test
	/*{
		// http://xcb.freedesktop.org/tutorial/mousecursors/
		// https://github.com/eBrnd/i3lock-color/blob/master/xcb.c      create_cursor()
		// http://xcb-util.sourcearchive.com/documentation/0.3.3-1/group__xcb____image__t_g029605b47d6ab95eac66b125a9a7dd64.html
		// https://en.wikipedia.org/wiki/X_BitMap#Format

		xcb_pixmap_t bitmap;
		xcb_pixmap_t mask;
		xcb_cursor_t cursor;

		uint32_t width = 32, height = 32;

		// in the example: curs_bits is unsigned char [50]
		// width and height are 11 and 19
		// The bitmap data is assumed to be in xbm format (i.e., 8-bit scanline unit, LSB-first, 8-bit pad). If depth is greater than 1, the bitmap will be expanded to a pixmap using the given foreground and background pixels fg and bg.

		unsigned char curs_bits[...];
		unsigned char mask_bits[...];

		bitmap = xcb_create_pixmap_from_bitmap_data(connection, window,
													curs_bits,
													width, height,
													1,
													screen->white_pixel, screen->black_pixel,
													0);
		mask = xcb_create_pixmap_from_bitmap_data(connection, window,
												  mask_bits,
												  width, height,
												  1,
												  screen->white_pixel, screen->black_pixel,
												  0);

		cursor = xcb_generate_id(connection);
		xcb_create_cursor(connection,
						  cursor,
						  bitmap,
						  mask,
						  65535, 65535, 65535,
						  0, 0, 0,
						  0, 0);

		xcb_free_pixmap(connection, bitmap);
		xcb_free_pixmap(connection, mask);
	}*/
}

static void show_resource(const struct image *restrict image, int treasury, int income, int expense, unsigned y)
{
	unsigned x;
	char buffer[32], *end; // TODO make sure this is enough

	image_draw(image, PANEL_X, y);
	end = format_uint(buffer, treasury, 10);

	x = PANEL_X + image->width;
	x += display_string(buffer, end - buffer, x, y, &font12, Black);
	if (income)
	{
		end = format_sint(buffer, income);
		x += display_string(buffer, end - buffer, x, y, &font12, Ally);
	}
	if (expense)
	{
		end = format_sint(buffer, -expense);
		x += display_string(buffer, end - buffer, x, y, &font12, Enemy);
	}
}

static void tooltip_cost(const char *restrict name, size_t name_length, const struct resources *restrict cost, unsigned time)
{
	char buffer[16];
	size_t length;

	unsigned offset = 120;

	display_string(name, name_length, TOOLTIP_X, TOOLTIP_Y, &font12, White);

	if (cost->gold)
	{
		image_draw(&image_gold, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, cost->gold, 10) - (uint8_t *)buffer;
		display_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->food)
	{
		image_draw(&image_food, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, cost->food, 10) - (uint8_t *)buffer;
		display_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->wood)
	{
		image_draw(&image_wood, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, cost->wood, 10) - (uint8_t *)buffer;
		display_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->stone)
	{
		image_draw(&image_stone, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, cost->stone, 10) - (uint8_t *)buffer;
		display_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->iron)
	{
		image_draw(&image_iron, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, cost->iron, 10) - (uint8_t *)buffer;
		display_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}

	image_draw(&image_time, TOOLTIP_X + offset, TOOLTIP_Y);
	offset += 16;

	length = format_uint(buffer, time, 10) - (uint8_t *)buffer;
	display_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
	offset += 40;
}

void if_map(const void *argument, const struct game *game)
{
	const struct state_map *state = argument;

	// clear window
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// display current player's color
	draw_rectangle(PANEL_X - 4, PANEL_Y - 4, PANEL_WIDTH + 8, PANEL_HEIGHT + 8, Player + state->player);
	draw_rectangle(PANEL_X - 3, PANEL_Y - 3, PANEL_WIDTH + 6, PANEL_HEIGHT + 6, Player + state->player);
	draw_rectangle(PANEL_X - 2, PANEL_Y - 2, PANEL_WIDTH + 4, PANEL_HEIGHT + 4, Player + state->player);

	// Display panel background pattern.
	display_image(&image_panel, PANEL_X, PANEL_Y, PANEL_WIDTH, PANEL_HEIGHT);

	// show map in black
	display_rectangle(MAP_X, MAP_Y, MAP_WIDTH, MAP_HEIGHT, Black);

	size_t x, y;
	size_t offset;
	struct pawn *p;

	size_t i, j;

	// Map

	struct resources income = {0}, expenses = {0};
	const struct troop *slot;

	// Determine which regions to show.
	unsigned char region_visible[REGIONS_LIMIT] = {0};
	for(i = 0; i < regions_count; ++i)
	{
		if (game->players[regions[i].owner].alliance == game->players[state->player].alliance)
		{
			region_visible[i] = 1;

			// Make the neighboring regions visible when a watch tower is built.
			if (regions[i].built & BUILDING_WATCH_TOWER)
			{
				for(j = 0; j < NEIGHBORS_LIMIT; ++j)
				{
					struct region *neighbor = regions[i].neighbors[j];
					if (neighbor) region_visible[neighbor->index] = 1;
				}
			}
		}
	}

	for(i = 0; i < regions_count; ++i)
	{
		// Fill each region with the color of its owner (or the color indicating unexplored).
		if (region_visible[i]) glColor4ubv(display_colors[Player + regions[i].owner]);
		else glColor4ubv(display_colors[Unexplored]);
		display_polygon(regions[i].location, MAP_X, MAP_Y);

		// Remember income and expenses.
		if (regions[i].owner == state->player) region_income(regions + i, &income);
		for(slot = regions[i].slots; slot; slot = slot->_next)
			if (slot->player == state->player)
				resource_add(&expenses, &slot->unit->expense);
	}
	for(i = 0; i < regions_count; ++i)
	{
		// Draw region borders.
		glColor4ubv(display_colors[Black]);
		glBegin(GL_LINE_STRIP);
		for(j = 0; j < regions[i].location->vertices_count; ++j)
			glVertex2f(MAP_X + regions[i].location->points[j].x, MAP_Y + regions[i].location->points[j].y);
		glEnd();
	}

	if (state->region >= 0)
	{
		const struct region *region = regions + state->region;
		unsigned progress;

		// Display region owner's flag and name of the region.
		if (region_visible[region->index])
		{
			display_rectangle(PANEL_X + 4, PANEL_Y + 4, 24, 12, Player + region->owner);
			image_draw(&image_flag, PANEL_X, PANEL_Y);
		}
		display_string(region->name, region->name_length, PANEL_X + image_flag.width + MARGIN, PANEL_Y + (image_flag.height - font12.height) / 2, &font12, Black);

		// Display the slots at the selected region.
		if (game->players[state->player].alliance == game->players[region->owner].alliance)
		{
			unsigned char self_count = 0, ally_count = 0;
			enum color color_text;
			for(slot = region->slots; slot; slot = slot->_next)
			{
				if (slot->player == state->player)
				{
					if (!self_count) display_rectangle(PANEL_X, SLOT_Y(0) - 3, PANEL_WIDTH, FIELD_SIZE + MARGIN + 14, Self);
					x = self_count++;
					y = 0;
					offset = state->self_offset;
					color_text = Black;
				}
				else
				{
					if (!ally_count) display_rectangle(PANEL_X, SLOT_Y(1) - 3, PANEL_WIDTH, FIELD_SIZE + MARGIN + 14, Ally);
					x = ally_count++;
					y = 1;
					offset = state->ally_offset;
					color_text = White;
				}

				if ((x >= offset) && (x < offset + SLOTS_VISIBLE)) // if the unit is visible
				{
					x -= offset;
					display_unit(slot->unit->index, SLOT_X(x), SLOT_Y(y), Player + slot->player, color_text, slot->count);
					if (slot == state->troop) draw_rectangle(SLOT_X(x) - 1, SLOT_Y(y) - 1, FIELD_SIZE + 2, FIELD_SIZE + 2, White);
				}

				// Draw the destination of each moving slot.
				if ((slot->player == state->player) && (!state->troop || (slot == state->troop)) && (slot->move->index != state->region))
				{
					struct point from = {slot->location->center.x, slot->location->center.y};
					struct point to = {slot->move->center.x, slot->move->center.y};
					display_arrow(from, to, MAP_X, MAP_Y, Self);
				}
			}

			// Display scroll buttons.
			if (state->self_offset) image_draw(&image_scroll_left, SLOT_X(0) - 1 - SCROLL, SLOT_Y(0));
			if ((self_count - state->self_offset) > SLOTS_VISIBLE) image_draw(&image_scroll_right, SLOT_X(SLOTS_VISIBLE), SLOT_Y(0));
			if (state->ally_offset) image_draw(&image_scroll_left, SLOT_X(0) - 1 - SCROLL, SLOT_Y(1));
			if ((ally_count - state->ally_offset) > SLOTS_VISIBLE) image_draw(&image_scroll_right, SLOT_X(SLOTS_VISIBLE), SLOT_Y(1));

			for(i = 0; i < buildings_count; ++i)
			{
				struct point position = if_position(Building, i);

				// Don't display the building if its requirements are not satisfied.
				if ((region->built & buildings[i].requires) != buildings[i].requires) continue;

				if (region->built & (1 << i)) image_draw(image_buildings + i, position.x, position.y);
				else image_draw(image_buildings_gray + i, position.x, position.y);
			}
			if (region->construct >= 0)
			{
				struct point position = if_position(Building, region->construct);
				show_progress(region->construct_time, buildings[region->construct].time, position.x, position.y, object_group[Building].width, object_group[Building].height);
				image_draw(&image_construction, position.x, position.y);
			}
		}

		if (state->player == region->owner)
		{
			display_string(S("train:"), PANEL_X + 2, TRAIN_Y + (FIELD_SIZE - font12.height) / 2, &font12, Black);

			// Display train queue.
			size_t index;
			for(index = 0; index < TRAIN_QUEUE; ++index)
				if (region->train[index])
				{
					display_unit(region->train[index]->index, TRAIN_X(index), TRAIN_Y, White, 0, 0);
					show_progress((index ? 0 : region->train_time), region->train[0]->time, TRAIN_X(index), TRAIN_Y, FIELD_SIZE, FIELD_SIZE);
				}
				else display_rectangle(TRAIN_X(index), TRAIN_Y, FIELD_SIZE, FIELD_SIZE, Black);

			// Display units available for training.
			for(index = 0; index < game->units_count; ++index)
			{
				// Don't display the unit if its requirements are not satisfied.
				if ((region->built & game->units[index].requires) != game->units[index].requires) continue;

				display_unit(index, INVENTORY_X(index), INVENTORY_Y, Player, 0, 0);
			}

			// Display tooltip for the hovered object.
			switch (state->hover_object)
			{
			case HOVER_UNIT:
				{
					const struct unit *unit = game->units + state->hover.unit;
					tooltip_cost(unit->name, unit->name_length, &unit->cost, unit->time);
				}
				break;
			case HOVER_BUILDING:
				if (!building_built(region, state->hover.building))
				{
					const struct building *building = buildings + state->hover.building;
					tooltip_cost(building->name, building->name_length, &building->cost, building->time);
				}
				break;
			}
		}
	}

	// Treasury
	struct resources *treasury = &game->players[state->player].treasury;
	show_resource(&image_gold, treasury->gold, income.gold, expenses.gold, RESOURCE_GOLD);
	show_resource(&image_food, treasury->food, income.food, expenses.food, RESOURCE_FOOD);
	show_resource(&image_wood, treasury->wood, income.wood, expenses.wood, RESOURCE_WOOD);
	show_resource(&image_stone, treasury->stone, income.stone, expenses.stone, RESOURCE_STONE);
	show_resource(&image_iron, treasury->iron, income.iron, expenses.iron, RESOURCE_IRON);

	glFlush();
	glXSwapBuffers(display, drawable);
}

void if_set(struct battlefield field[BATTLEFIELD_WIDTH][BATTLEFIELD_HEIGHT], struct battle *b)
{
	battle = b;
	battlefield = field;
}

void if_regions_input(struct game *restrict game)
{
	regions = game->regions;
	regions_count = game->regions_count;

	glBindFramebuffer(GL_FRAMEBUFFER, map_framebuffer);

	glClear(GL_COLOR_BUFFER_BIT);

	size_t i, j;
	for(i = 0; i < regions_count; ++i)
	{
		glColor3ub(255, 255, i);
		display_polygon(regions[i].location, 0, 0);
	}

	glFlush();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void if_term(void)
{
	XFree(keymap);

	// TODO put this somewhere
	/*size_t i;
	for(i = 0; i < regions_count; ++i)
		free(regions[i].location);*/

	glDeleteRenderbuffers(1, &map_renderbuffer);
	glDeleteFramebuffers(1, &map_framebuffer);

	glXDestroyWindow(display, drawable);
	xcb_destroy_window(connection, window);
	glXDestroyContext(display, context);
	XCloseDisplay(display);
}
