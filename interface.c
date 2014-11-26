#include <stdlib.h>

#define GL_GLEXT_PROTOTYPES

//#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glext.h>

#include <xcb/xcb.h>

//#include <X11/Xlib-xcb.h>

#include "format.h"
#include "battle.h"
#include "image.h"
#include "interface.h"

// http://xcb.freedesktop.org/opengl/
// http://xcb.freedesktop.org/tutorial/events/
// http://techpubs.sgi.com/library/dynaweb_docs/0640/SGI_Developer/books/OpenGL_Porting/sgi_html/ch04.html
// http://tronche.com/gui/x/xlib/graphics/font-metrics/
// http://open.gl
// http://www.opengl.org/sdk/docs/man2/

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

#define FIELD_SIZE 32

#define CTRL_X 768
#define CTRL_Y 32

#define PAWN_MARGIN 4

#define STRING(s) (s), sizeof(s) - 1

#define PANEL_X 0
#define PANEL_Y 32

#define PANEL_WIDTH 248
#define PANEL_HEIGHT 760

#define RESOURCE_GOLD 660
#define RESOURCE_FOOD 680
#define RESOURCE_WOOD 700
#define RESOURCE_IRON 720
#define RESOURCE_ROCK 740

#define MAP_X 256
#define MAP_Y 0
#define MAP_WIDTH 768
#define MAP_HEIGHT 768

#define BATTLE_X 0
#define BATTLE_Y 0

// TODO compatibility with OpenGL 2.1 (used in MacOS X)
#define glGenFramebuffers(...) glGenFramebuffersEXT(__VA_ARGS__)
#define glGenRenderbuffers(...) glGenRenderbuffersEXT(__VA_ARGS__)
#define glBindFramebuffer(...) glBindFramebufferEXT(__VA_ARGS__)
#define glBindRenderbuffer(...) glBindRenderbufferEXT(__VA_ARGS__)
#define glRenderbufferStorage(...) glRenderbufferStorageEXT(__VA_ARGS__)
#define glFramebufferRenderbuffer(...) glFramebufferRenderbufferEXT(__VA_ARGS__)

static Display *display;
static xcb_connection_t *connection;
static xcb_window_t window;
static GLXDrawable drawable;
static GLXContext context;

// TODO Create a struct that stores all the information about the battle (battlefield, players, etc.)
static struct pawn *(*battlefield)[BATTLEFIELD_WIDTH];

static const struct unit *units;
static size_t units_count;

static struct region *restrict regions;
static size_t regions_count;

static struct image image_move_destination, image_shoot_destination, image_selected, image_flag;
static struct image image_units[2]; // TODO the array must be enough to hold units_count units

static struct
{
	unsigned char player; // current player
	unsigned char x, y; // current field
	struct pawn *pawn; // TODO put this in the union
	union
	{
		struct slot *slot;
	} selected;

	int region;
} state;

struct area
{
	unsigned left, right, top, bottom;
	void (*callback)(const xcb_button_release_event_t *restrict, unsigned, unsigned, const struct player *restrict);
};

static GLuint map_framebuffer, map_renderbuffer;

struct font font;

static int keysyms_per_keycode;
static int keycode_min, keycode_max;
static KeySym *keymap;

static void if_reshape(int width, int height)
{
	glViewport(0, 0, width, height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, width, height, 0, 0, 1);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void if_init(void)
{
	display = XOpenDisplay(0);
	if (!display) return;

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
	xcb_screen_t *screen = screen_iter.data;

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

	uint32_t eventmask = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE;
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

	font_init(display, &font); // TODO error check
	glFont(font.base);

	// enable transparency
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// TODO set window title, border, etc.

	// TODO full screen

	if_reshape(SCREEN_WIDTH, SCREEN_HEIGHT); // TODO call this after resize

	image_load_png(&image_move_destination, "img/move_destination.png");
	image_load_png(&image_shoot_destination, "img/shoot_destination.png");
	image_load_png(&image_selected, "img/selected.png");
	image_load_png(&image_flag, "img/flag.png");

	image_load_png(&image_units[0], "img/peasant.png");
	image_load_png(&image_units[1], "img/archer.png");

	// TODO handle modifier keys
	// TODO handle dead keys
	// TODO the keyboard mappings don't work as expected for different keyboard layouts

	// Initialize keyboard mapping table.
	XDisplayKeycodes(display, &keycode_min, &keycode_max);
	keymap = XGetKeyboardMapping(display, keycode_min, (keycode_max - keycode_min + 1), &keysyms_per_keycode);
	if (!keymap) return; // TODO error

	glGenFramebuffers(1, &map_framebuffer);
	glGenRenderbuffers(1, &map_renderbuffer);

	glBindFramebuffer(GL_FRAMEBUFFER, map_framebuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, map_renderbuffer);

	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB8, MAP_WIDTH, MAP_HEIGHT);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, map_renderbuffer);

	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	return;

error:
	XCloseDisplay(display);
}

static void display_unit(size_t unit, unsigned x, unsigned y, enum color color, unsigned count)
{
	display_rectangle(x, y, FIELD_SIZE, FIELD_SIZE, color);
	image_draw(&image_units[unit], x, y);

	if (count)
	{
		char buffer[16];
		size_t length = format_uint(buffer, count) - buffer;
		display_string(buffer, length, x + (FIELD_SIZE - (length * 10)) / 2, y + FIELD_SIZE, Black);
	}
}

void if_battle(const struct player *restrict players)
{
	// clear window
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// show the right panel in gray
	display_rectangle(768, 0, 256, 768, Gray);

	// draw rectangle with current player's color
	display_rectangle(768, 0, 256, 16, Player + state.player);

	size_t x, y;
	const struct pawn *p;

	// Battlefield

	// color every other field in white
	/*for(y = 0; y < BATTLEFIELD_HEIGHT; y += 1)
		for(x = y % 2; x < BATTLEFIELD_WIDTH; x += 2)
			display_rectangle(x * FIELD_SIZE, y * FIELD_SIZE, FIELD_SIZE, FIELD_SIZE, White);*/
	display_rectangle(0, 0, BATTLEFIELD_WIDTH * FIELD_SIZE, BATTLEFIELD_HEIGHT * FIELD_SIZE, B0);

	// display pawns
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			if (p = battlefield[y][x])
			{
				size_t unit_index = 0, unit_index_current;
				enum color color = Enemy;
				int show_ally = 0, show_enemy = 0;

				// Determine what to display on the field.
				do
				{
					unit_index_current = p->slot->unit->index;

					if (p->slot->player == state.player)
					{
						if (color != Self)
						{
							unit_index = unit_index_current;
							color = Self;
						}
						else if (unit_index_current > unit_index) unit_index = unit_index_current;
					}
					else if (players[p->slot->player].alliance == players[state.player].alliance)
					{
						show_ally = 1;
						if (color != Self)
						{
							if (color != Ally)
							{
								unit_index = unit_index_current;
								color = Ally;
							}
							else if (unit_index_current > unit_index) unit_index = unit_index_current;
						}
					}
					else
					{
						show_enemy = 1;
						if ((color == Enemy) && (unit_index_current > unit_index)) unit_index = unit_index_current;
					}
				} while (p = p->_next);

				display_rectangle(x * FIELD_SIZE, y * FIELD_SIZE, FIELD_SIZE, FIELD_SIZE, color);
				image_draw(&image_units[unit_index], x * FIELD_SIZE, y * FIELD_SIZE);

				if (show_ally && (color != Ally))
					display_rectangle(x * FIELD_SIZE + 26, y * FIELD_SIZE, 6, 6, Ally);
				if (show_enemy && (color != Enemy))
					display_rectangle(x * FIELD_SIZE + 26, y * FIELD_SIZE + 26, 6, 6, Enemy);
			}
		}

	unsigned row, column;

	// Display information about the selected field.
	// Show each player's units on a separate line. Order: Self, Ally, Enemy.
	signed char positions[PLAYERS_LIMIT];
	unsigned char indexes[PLAYERS_LIMIT] = {0};
	unsigned self_count = 0, allies_count = 0, enemies_count = 0;
	if ((state.x < BATTLEFIELD_WIDTH) && (state.y < BATTLEFIELD_HEIGHT) && battlefield[state.y][state.x])
	{
		image_draw(&image_selected, state.x * FIELD_SIZE, state.y * FIELD_SIZE);

		memset(positions, -1, sizeof(positions) * sizeof(*positions));

		// Count the number of players in each category (Self, Ally, Enemy).
		// Initialize their display positions.
		p = battlefield[state.y][state.x];
		do
		{
			if (p->slot->player == state.player)
			{
				positions[p->slot->player] = 0;
				self_count = 1;
			}
			else if (players[p->slot->player].alliance == players[state.player].alliance)
			{
				if (positions[p->slot->player] < 0)
					positions[p->slot->player] = allies_count++;
			}
			else
			{
				if (positions[p->slot->player] < 0)
					positions[p->slot->player] = enemies_count++;
			}
		} while (p = p->_next);

		if (self_count) display_rectangle(CTRL_X, CTRL_Y, (FIELD_SIZE + 4) * 7 + 4, FIELD_SIZE + 4 + 16, Self);
		if (allies_count) display_rectangle(CTRL_X, CTRL_Y + self_count * (FIELD_SIZE + 4 + 16), (FIELD_SIZE + 4) * 7 + 4, allies_count * (FIELD_SIZE + 4 + 16), Ally);
		if (enemies_count) display_rectangle(CTRL_X, CTRL_Y + (self_count + allies_count) * (FIELD_SIZE + 4 + 16), (FIELD_SIZE + 4) * 7 + 4, enemies_count * (FIELD_SIZE + 4 + 16), Enemy);

		// Display the pawns on the selected field.
		p = battlefield[state.y][state.x];
		do
		{
			// TODO support more than 7 pawns on a row
			row = ((p->slot->player != state.player) ? self_count : 0) + ((players[p->slot->player].alliance != players[state.player].alliance) ? allies_count : 0) + positions[p->slot->player];
			column = indexes[p->slot->player]++;

			x = CTRL_X + 4 + column * (FIELD_SIZE + 4);
			y = CTRL_Y + 2 + row * (FIELD_SIZE + 4 + 16);

			display_unit(p->slot->unit->index, x, y, Player + p->slot->player, p->slot->count);
			if (p == state.pawn) draw_rectangle(x - 1, y - 1, FIELD_SIZE + 2, FIELD_SIZE + 2, White);

			// Show destination of each moving pawn.
			// TODO don't draw at the same place twice
			if (!state.pawn || (p == state.pawn))
				if (p->slot->player == state.player)
				{
					if ((p->move.x[1] != p->move.x[0]) || (p->move.y[1] != p->move.y[0]))
					{
						struct point from = {p->move.x[0] * FIELD_SIZE + FIELD_SIZE / 2, p->move.y[0] * FIELD_SIZE + FIELD_SIZE / 2};
						struct point to = {p->move.x[1] * FIELD_SIZE + FIELD_SIZE / 2, p->move.y[1] * FIELD_SIZE + FIELD_SIZE / 2};
						display_arrow(from, to, BATTLE_X, BATTLE_Y, Self);
					}
					else if ((p->shoot.x >= 0) && (p->shoot.y >= 0) && ((p->shoot.x != p->move.x[0]) || (p->shoot.y != p->move.y[0])))
						image_draw(&image_shoot_destination, p->shoot.x * FIELD_SIZE, p->shoot.y * FIELD_SIZE);
				}
		} while (p = p->_next);
	}

	glFlush();
	glXSwapBuffers(display, drawable);
}

static void show_resource(const char *restrict name, size_t name_length, int treasury, int income, int expense, unsigned y)
{
	char buffer[32]; // TODO make sure this is enough
	size_t length;
	unsigned offset;

	length = format_uint(format_bytes(buffer, name, name_length), treasury) - buffer;
	display_string(buffer, length, PANEL_X, y, White);
	offset = length;

	if (income)
	{
		length = format_sint(buffer, income) - buffer;
		display_string(buffer, length, PANEL_X + offset * 10, y, Ally);
		offset += length;
	}

	if (expense)
	{
		length = format_sint(buffer, expense) - buffer;
		display_string(buffer, length, PANEL_X + offset * 10, y, Enemy);
		// offset += length;
	}
}

void if_map(const struct player *restrict players) // TODO finish this
{
	// clear window
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// display current player's color
	display_rectangle(0, 0, 256, 16, Player + state.player);

	// show the panel in black
	display_rectangle(PANEL_X, PANEL_Y, PANEL_WIDTH, PANEL_HEIGHT, Black);

	// show map in black
	display_rectangle(MAP_X, MAP_Y, MAP_WIDTH, MAP_HEIGHT, Black);

	size_t x, y;
	struct pawn *p;

	// Map

	struct resources income = {0}, expenses = {0};
	const struct slot *slot;

	size_t i, j;
	for(i = 0; i < regions_count; ++i)
	{
		// Fill each region with the color of its owner.
		glColor4ubv(display_colors[Player + regions[i].owner]);
		display_polygon(regions[i].location, MAP_X, MAP_Y);

		// Remember income and expenses.
		if (regions[i].owner == state.player) resource_change(&income, &regions[i].income);
		for(slot = regions[i].slots; slot; slot = slot->_next)
			if (slot->player == state.player)
				resource_change(&expenses, &slot->unit->expense);
	}
	for(i = 0; i < regions_count; ++i)
	{
		// Draw region borders.
		glColor4ubv(display_colors[Black]);
		glBegin(GL_LINE_STRIP);
		for(j = 0; j < regions[i].location->vertices; ++j)
			glVertex2f(MAP_X + regions[i].location->points[j].x, MAP_Y + regions[i].location->points[j].y);
		glEnd();
	}

	if (state.region >= 0)
	{
		const struct region *region = regions + state.region;

		display_string(STRING("owner:"), PANEL_X, PANEL_Y - 16, White);
		display_rectangle(PANEL_X + 7 * font.width, PANEL_Y - 16 + ((int)font.height - 16) / 2, 16, 16, Player + region->owner);

		// Display the slots at the current region.
		if (players[state.player].alliance == players[region->owner].alliance)
		{
			unsigned char position_x[PLAYERS_LIMIT] = {0}, position_y[PLAYERS_LIMIT] = {0};
			unsigned self_count = 0, allies_count = 0;
			for(slot = region->slots; slot; slot = slot->_next)
			{
				if (slot->player == state.player)
				{
					if (!self_count)
					{
						self_count = 1;
						display_rectangle(PANEL_X, PANEL_Y, 7 * (FIELD_SIZE + 4) + 4, FIELD_SIZE + 4 + 16, Self);
					}
				}
				else
				{
					if (!position_y[slot->player])
					{
						allies_count += 1;
						display_rectangle(PANEL_X, PANEL_Y + allies_count * (FIELD_SIZE + 4 + 16), 7 * (FIELD_SIZE + 4) + 4, FIELD_SIZE + 4 + 16, Ally);
						position_y[slot->player] = allies_count;
					}
				}

				// TODO make this work for more than 6 slots

				x = PANEL_X + 4 + position_x[slot->player] * (FIELD_SIZE + 4);
				y = PANEL_Y + 2 + position_y[slot->player] * (FIELD_SIZE + 4 + 16);
				position_x[slot->player] += 1;

				display_unit(slot->unit->index, x, y, Player + slot->player, slot->count);
				if (slot == state.selected.slot) draw_rectangle(x - 1, y - 1, FIELD_SIZE + 2, FIELD_SIZE + 2, White);

				// Draw the destination of each moving slot.
				if ((slot->player == state.player) && (!state.selected.slot || (slot == state.selected.slot)) && (slot->move->index != state.region))
				{
					struct point from = {slot->location->center.x, slot->location->center.y};
					struct point to = {slot->move->center.x, slot->move->center.y};
					display_arrow(from, to, MAP_X, MAP_Y, Self);
				}
			}
		}

		if (state.player == region->owner)
		{
			// Display train queue.
			size_t index;
			for(index = 0; index < TRAIN_QUEUE; ++index)
				if (region->train[index])
				{
					display_unit(region->train[index]->index, PANEL_X + ((FIELD_SIZE + PAWN_MARGIN) * index), PANEL_Y + 196, White, 0);
					display_rectangle(PANEL_X + ((FIELD_SIZE + PAWN_MARGIN) * index), PANEL_Y + 196, FIELD_SIZE, FIELD_SIZE, Progress);
				}
				else break;

			// Display units available for training.
			display_unit(0, PANEL_X + (FIELD_SIZE + 8) * 0, PANEL_Y + 260, White, 0);
			display_unit(1, PANEL_X + (FIELD_SIZE + 8) * 1, PANEL_Y + 260, White, 0);
		}
	}

	// Treasury
	show_resource(STRING("gold: "), players[state.player].treasury.gold, income.gold, expenses.gold, RESOURCE_GOLD);
	show_resource(STRING("food: "), players[state.player].treasury.food, income.food, expenses.food, RESOURCE_FOOD);
	show_resource(STRING("wood: "), players[state.player].treasury.wood, income.wood, expenses.wood, RESOURCE_WOOD);
	show_resource(STRING("iron: "), players[state.player].treasury.iron, income.iron, expenses.iron, RESOURCE_IRON);
	show_resource(STRING("rock: "), players[state.player].treasury.rock, income.rock, expenses.rock, RESOURCE_ROCK);

	glFlush();
	glXSwapBuffers(display, drawable);
}

void if_set(struct pawn *bf[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	battlefield = bf;
}

void if_regions(struct region *restrict reg, size_t count, const struct unit *u, size_t u_count)
{
	regions = reg;
	regions_count = count;

	units = u;
	units_count = u_count;

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

static int input_in(xcb_button_release_event_t *restrict mouse, const struct area *restrict area)
{
	return ((area->left <= mouse->event_x) && (mouse->event_x <= area->right) && (area->top <= mouse->event_y) && (mouse->event_y <= area->bottom));
}

static void input_region(const xcb_button_release_event_t *restrict mouse, unsigned x, unsigned y, const struct player *restrict players)
{
	// TODO write this function better

	// Get the clicked region.
	GLubyte pixel_color[3];
	glBindFramebuffer(GL_FRAMEBUFFER, map_framebuffer);
	glReadPixels(x, MAP_HEIGHT - y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel_color);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (mouse->detail == 1)
	{
		if (!pixel_color[0]) state.region = -1;
		else state.region = pixel_color[2];

		state.selected.slot = 0;
	}
	else if (mouse->detail == 3)
	{
		struct region *region = regions + state.region;
		struct slot *slot;

		if (!pixel_color[0]) return;

		unsigned index;
		struct region *destination = regions + pixel_color[2];
		if (destination == region) goto valid;
		for(index = 0; index < 8; ++index)
			if (destination == region->neighbors[index])
				goto valid;
		return;

valid:
		if (state.selected.slot)
		{
			// Set the move destination of the selected slot.
			slot = state.selected.slot;
			if (state.player == slot->player)
				slot->move = regions + pixel_color[2];
		}
		else
		{
			// Set the move destination of all slots in the region.
			for(slot = region->slots; slot; slot = slot->_next)
				if (state.player == slot->player)
					slot->move = regions + pixel_color[2];
		}
	}
}

static void input_train(const xcb_button_release_event_t *restrict mouse, unsigned x, unsigned y, const struct player *restrict players)
{
	if (state.player != regions[state.region].owner) return;
	if ((x % (FIELD_SIZE + PAWN_MARGIN)) >= FIELD_SIZE) return;

	if (mouse->detail == 1)
	{
		size_t unit = x / (FIELD_SIZE + PAWN_MARGIN);
		if (unit >= units_count) return;

		struct unit **train = regions[state.region].train;
		size_t index;
		for(index = 0; index < TRAIN_QUEUE; ++index)
			if (!train[index])
			{
				train[index] = (struct unit *)(units + unit); // TODO fix this cast
				break;
			}
	}
}

static void input_dismiss(const xcb_button_release_event_t *restrict mouse, unsigned x, unsigned y, const struct player *restrict players)
{
	if (state.player != regions[state.region].owner) return;
	if ((x % (FIELD_SIZE + PAWN_MARGIN)) >= FIELD_SIZE) return;

	if (mouse->detail == 1)
	{
		struct unit **train = regions[state.region].train;

		size_t index;
		for(index = (x / (FIELD_SIZE + PAWN_MARGIN) + 1); index < TRAIN_QUEUE; ++index)
			train[index - 1] = train[index];
		train[TRAIN_QUEUE - 1] = 0;
	}
}

static void input_slot(const xcb_button_release_event_t *restrict mouse, unsigned x, unsigned y, const struct player *restrict players)
{
	if (state.region < 0) return;
	struct slot *slot = regions[state.region].slots;
	if (!slot) return; // no field selected

	if (mouse->detail == 1)
	{
		if ((x % (FIELD_SIZE + PAWN_MARGIN)) >= FIELD_SIZE) goto reset;

		// Select the clicked pawn.
		int offset = x / (FIELD_SIZE + PAWN_MARGIN);
		int found;
		while (1)
		{
			found = (slot->player == state.player);
			if (!found || offset)
			{
				slot = slot->_next;
				if (!slot) goto reset;
				if (found) offset -= 1;
			}
			else if (found) break;
		}
		state.selected.slot = slot;
	}

	return;

reset:
	// Make sure no slot is selected.
	state.selected.slot = 0;
}

int input_map(unsigned char player, const struct player *restrict players)
{
	state.player = player;

	state.region = -1;
	state.selected.slot = 0;

	if_map(players);

	KeySym *input;

	xcb_generic_event_t *event;
	xcb_button_release_event_t *mouse;
	xcb_key_press_event_t *keyboard;

	struct area areas[] = {
		{
			.left = MAP_X,
			.right = MAP_X + MAP_WIDTH - 1,
			.top = MAP_Y,
			.bottom = MAP_Y + MAP_HEIGHT - 1,
			.callback = input_region
		},
		{
			.left = PANEL_X,
			.right = PANEL_X + (FIELD_SIZE + 8) * units_count - 1,
			.top = PANEL_Y + 260,
			.bottom = PANEL_Y + 260 + FIELD_SIZE - 1,
			.callback = input_train
		},
		{
			.left = PANEL_X,
			.right = PANEL_X + TRAIN_QUEUE * (FIELD_SIZE + PAWN_MARGIN) - 1,
			.top = PANEL_Y + 196,
			.bottom = PANEL_Y + 196 + FIELD_SIZE - 1,
			.callback = input_dismiss
		},
		{
			.left = PANEL_X + 4,
			.right = PANEL_X + 4 + 7 * (FIELD_SIZE + PAWN_MARGIN) - PAWN_MARGIN - 1,
			.top = PANEL_Y,
			.bottom = PANEL_Y + FIELD_SIZE - 1,
			.callback = input_slot,
		}
	};
	size_t areas_count = sizeof(areas) / sizeof(*areas);

	size_t index;

	while (1)
	{
		event = xcb_wait_for_event(connection);
		// TODO consider using xcb_poll_for_event()
		if (!event) return -1;

		switch (event->response_type & ~0x80)
		{
		case XCB_BUTTON_PRESS:
			mouse = (xcb_button_release_event_t *)event;
			for(index = 0; index < areas_count; ++index)
				if (input_in(mouse, areas + index)) areas[index].callback(mouse, mouse->event_x - areas[index].left, mouse->event_y - areas[index].top, players);
		case XCB_EXPOSE:
			if_map(players);
			break;

		case XCB_KEY_PRESS:
			keyboard = (xcb_key_press_event_t *)event;
			input = keymap + (keyboard->detail - keycode_min) * keysyms_per_keycode;

			if (*input == 'q')
			{
				free(event);
				return -1;
			}
			else if (*input == 'n')
			{
				free(event);
				return 0;
			}
			break;

		case XCB_BUTTON_RELEASE:
			//printf("release: %d\n", (int)((xcb_button_release_event_t *)event)->detail);
			break;
		}

		free(event);
	}
}

static void input_pawn(const xcb_button_release_event_t *restrict mouse, unsigned x, unsigned y, const struct player *restrict players)
{
	if ((state.x == BATTLEFIELD_WIDTH) || (state.y == BATTLEFIELD_HEIGHT)) return;
	struct pawn *pawn = battlefield[state.y][state.x];
	if (!pawn) return; // no field selected

	if (mouse->detail == 1)
	{
		if ((x % (FIELD_SIZE + PAWN_MARGIN)) >= FIELD_SIZE) goto reset;

		// Select the clicked pawn.
		int offset = x / (FIELD_SIZE + PAWN_MARGIN);
		int found;
		while (1)
		{
			found = (pawn->slot->player == state.player);
			if (!found || offset)
			{
				pawn = pawn->_next;
				if (!pawn) goto reset;
				if (found) offset -= 1;
			}
			else if (found) break;
		}
		state.pawn = pawn;
	}

	return;

reset:
	// Make sure no pawn is selected.
	state.pawn = 0;
}

// Sets move destination of a pawn. Returns -1 if the current player is not allowed to move the pawn at the destination.
static int pawn_move(const struct player *restrict players, struct pawn *restrict pawn, unsigned x, unsigned y)
{
	// TODO support fast move
	if ((state.player == pawn->slot->player) && reachable(players, battlefield, pawn, x, y))
	{
		// Reset shoot commands.
		pawn->shoot.x = -1;
		pawn->shoot.y = -1;

		pawn->move.x[1] = x;
		pawn->move.y[1] = y;

		return 0;
	}
	else return -1;
}

// Sets shoot target of a pawn. Returns -1 if the current player is not allowed to shoot at the target with this pawn.
static int pawn_shoot(const struct player *restrict players, struct pawn *restrict pawn, unsigned x, unsigned y)
{
	if ((state.player == pawn->slot->player) && shootable(players, battlefield, pawn, x, y))
	{
		// Reset move commands.
		pawn->move.x[1] = pawn->move.x[0];
		pawn->move.y[1] = pawn->move.y[0];

		pawn->shoot.x = x;
		pawn->shoot.y = y;

		return 0;
	}
	else return -1;
}

static void input_field(const xcb_button_release_event_t *restrict mouse, unsigned x, unsigned y, const struct player *restrict players)
{
	x /= FIELD_SIZE;
	y /= FIELD_SIZE;

	// if (mouse->state & XCB_MOD_MASK_SHIFT) ; // TODO fast move

	if (mouse->detail == 1)
	{
		// Set current field.
		state.x = x;
		state.y = y;
		state.pawn = 0;
	}
	else if (mouse->detail == 3)
	{
		// shoot if CONTROL is pressed; move otherwise
		// If there is a pawn selected, apply the command just to it.
		// Otherwise apply the command to each pawn on the current field.
		if (state.pawn)
		{
			if (mouse->state & XCB_MOD_MASK_CONTROL) pawn_shoot(players, state.pawn, x, y);
			else pawn_move(players, state.pawn, x, y);
		}
		else
		{
			struct pawn *pawn;
			for(pawn = battlefield[state.y][state.x]; pawn; pawn = pawn->_next)
			{
				if (mouse->state & XCB_MOD_MASK_CONTROL) pawn_shoot(players, pawn, x, y);
				else pawn_move(players, pawn, x, y);
			}
		}
	}
}

int input_battle(unsigned char player, const struct player *restrict players)
{
	state.player = player;

	// Set current field to a field outside of the board.
	state.x = BATTLEFIELD_WIDTH;
	state.y = BATTLEFIELD_HEIGHT;

	state.pawn = 0;

	if_battle(players);

	KeySym *input;

	xcb_generic_event_t *event;
	xcb_button_release_event_t *mouse;

	struct area areas[] = {
		{
			.left = 0,
			.right = BATTLEFIELD_WIDTH * FIELD_SIZE - 1,
			.top = 0,
			.bottom = BATTLEFIELD_HEIGHT * FIELD_SIZE - 1,
			.callback = input_field
		},
		{
			.left = CTRL_X + 4,
			.right = CTRL_X + 4 + 7 * (FIELD_SIZE + 4) - 4 - 1, // TODO change this 7
			.top = CTRL_Y + 2,
			.bottom = CTRL_Y + 2 + FIELD_SIZE - 1,
			.callback = input_pawn
		},
	};
	size_t areas_count = sizeof(areas) / sizeof(*areas);

	size_t index;

	while (1)
	{
		event = xcb_wait_for_event(connection);
		// TODO consider using xcb_poll_for_event()
		if (!event) return -1;

		switch (event->response_type & ~0x80)
		{
		case XCB_BUTTON_PRESS:
			mouse = (xcb_button_release_event_t *)event;
			for(index = 0; index < areas_count; ++index)
				if (input_in(mouse, areas + index)) areas[index].callback(mouse, mouse->event_x - areas[index].left, mouse->event_y - areas[index].top, players);
		case XCB_EXPOSE:
			if_battle(players);
			break;

		case XCB_KEY_PRESS:
			input = keymap + (((xcb_key_press_event_t *)event)->detail - keycode_min) * keysyms_per_keycode;

			//printf("%d %c %c %c %c\n", (int)*input, (int)input[0], (int)input[1], (int)input[2], (int)input[3]);

			/*if (*input == 'q')
			{
				free(event);
				return -1;
			}
			else*/ if (*input == 'n')
			{
				free(event);
				return 0;
			}
			break;

		case XCB_BUTTON_RELEASE:
			//printf("release: %d\n", (int)((xcb_button_release_event_t *)event)->detail);
			break;
		}

		free(event);
	}
}
