#include <stdlib.h>

#define GL_GLEXT_PROTOTYPES

//#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glext.h>

#include <xcb/xcb.h>

//#include <X11/Xlib-xcb.h>

#include "types.h"
#include "json.h"
#include "format.h"
#include "map.h"
#include "battle.h"
#include "image.h"
#include "input.h"
#include "interface.h"

// http://xcb.freedesktop.org/opengl/
// http://xcb.freedesktop.org/tutorial/events/
// http://techpubs.sgi.com/library/dynaweb_docs/0640/SGI_Developer/books/OpenGL_Porting/sgi_html/ch04.html
// http://tronche.com/gui/x/xlib/graphics/font-metrics/
// http://open.gl
// http://www.opengl.org/sdk/docs/man2/

#define S(s) (s), sizeof(s) - 1

#define RESOURCE_GOLD 660
#define RESOURCE_FOOD 680
#define RESOURCE_WOOD 700
#define RESOURCE_IRON 720
#define RESOURCE_ROCK 740

// TODO compatibility with OpenGL 2.1 (used in MacOS X)
#define glGenFramebuffers(...) glGenFramebuffersEXT(__VA_ARGS__)
#define glGenRenderbuffers(...) glGenRenderbuffersEXT(__VA_ARGS__)
#define glBindFramebuffer(...) glBindFramebufferEXT(__VA_ARGS__)
#define glBindRenderbuffer(...) glBindRenderbufferEXT(__VA_ARGS__)
#define glRenderbufferStorage(...) glRenderbufferStorageEXT(__VA_ARGS__)
#define glFramebufferRenderbuffer(...) glFramebufferRenderbufferEXT(__VA_ARGS__)

static Display *display;
static xcb_window_t window;
static GLXDrawable drawable;
static GLXContext context;

xcb_connection_t *connection;
KeySym *keymap;
int keysyms_per_keycode;
int keycode_min, keycode_max;
GLuint map_framebuffer;

// TODO Create a struct that stores all the information about the battle (battlefield, players, etc.)
struct pawn *(*battlefield)[BATTLEFIELD_WIDTH];

struct region *restrict regions;
size_t regions_count;

static struct image image_move_destination, image_shoot_destination, image_selected, image_flag, image_panel;
static struct image image_units[3]; // TODO the array must be enough to hold units_count units
static struct image image_buildings[3]; // TODO the array must be big enough to hold buildings_count elements

static GLuint map_renderbuffer;

struct font font;

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
	image_load_png(&image_panel, "img/panel.png");

	image_load_png(&image_units[0], "img/peasant.png");
	image_load_png(&image_units[1], "img/archer.png");
	image_load_png(&image_units[2], "img/horse_rider.png");

	image_load_png(&image_buildings[0], "img/irrigation.png");
	image_load_png(&image_buildings[1], "img/lumbermill.png");
	image_load_png(&image_buildings[2], "img/mine.png");

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

void if_battle(const struct player *restrict players, const struct state *restrict state)
{
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

					if (p->slot->player == state->player)
					{
						if (color != Self)
						{
							unit_index = unit_index_current;
							color = Self;
						}
						else if (unit_index_current > unit_index) unit_index = unit_index_current;
					}
					else if (players[p->slot->player].alliance == players[state->player].alliance)
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
	if ((state->x < BATTLEFIELD_WIDTH) && (state->y < BATTLEFIELD_HEIGHT) && battlefield[state->y][state->x])
	{
		image_draw(&image_selected, state->x * FIELD_SIZE, state->y * FIELD_SIZE);

		memset(positions, -1, sizeof(positions) * sizeof(*positions));

		// Count the number of players in each category (Self, Ally, Enemy).
		// Initialize their display positions.
		p = battlefield[state->y][state->x];
		do
		{
			if (p->slot->player == state->player)
			{
				positions[p->slot->player] = 0;
				self_count = 1;
			}
			else if (players[p->slot->player].alliance == players[state->player].alliance)
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
		p = battlefield[state->y][state->x];
		do
		{
			// TODO support more than 7 pawns on a row
			row = ((p->slot->player != state->player) ? self_count : 0) + ((players[p->slot->player].alliance != players[state->player].alliance) ? allies_count : 0) + positions[p->slot->player];
			column = indexes[p->slot->player]++;

			x = CTRL_X + 4 + column * (FIELD_SIZE + 4);
			y = CTRL_Y + 2 + row * (FIELD_SIZE + 4 + 16);

			display_unit(p->slot->unit->index, x, y, Player + p->slot->player, p->slot->count);
			if (p == state->selected.pawn) draw_rectangle(x - 1, y - 1, FIELD_SIZE + 2, FIELD_SIZE + 2, White);

			// Show destination of each moving pawn.
			// TODO don't draw at the same place twice
			if (!state->selected.pawn || (p == state->selected.pawn))
				if (p->slot->player == state->player)
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

double animation_timer;
int if_battle_animation(void)
{
	size_t x, y;
	const struct pawn *p;
	int moving = 0;

	// clear window
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Battlefield

	display_rectangle(0, 0, BATTLEFIELD_WIDTH * FIELD_SIZE, BATTLEFIELD_HEIGHT * FIELD_SIZE, B0);

	// display pawns
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			if (p = battlefield[y][x])
			{
				double px, py;
				double t;

				do
				{
					t = animation_timer / (p->move.t[1] - p->move.t[0]);
					if (t > 1) t = 1;
					else if ((p->move.x[1] != p->move.x[0]) || (p->move.y[1] != p->move.y[0])) moving = 1;

					px = p->move.x[1] * t + p->move.x[0] * (1 - t);
					py = p->move.y[1] * t + p->move.y[0] * (1 - t);

					display_rectangle(px * FIELD_SIZE, py * FIELD_SIZE, FIELD_SIZE, FIELD_SIZE, Player + p->slot->player);
					image_draw(&image_units[p->slot->unit->index], px * FIELD_SIZE, py * FIELD_SIZE);
				} while (p = p->_next);
			}
		}

	glFlush();
	glXSwapBuffers(display, drawable);

	return moving;
}

static void show_resource(const char *restrict name, size_t name_length, int treasury, int income, int expense, unsigned y)
{
	char buffer[32]; // TODO make sure this is enough
	size_t length;
	unsigned offset;

	length = format_uint(format_bytes(buffer, name, name_length), treasury) - buffer;
	display_string(buffer, length, PANEL_X, y, Black);
	offset = length;

	if (income)
	{
		length = format_sint(buffer, income) - buffer;
		display_string(buffer, length, PANEL_X + offset * 10, y, Ally);
		offset += length;
	}

	if (expense)
	{
		length = format_sint(buffer, -expense) - buffer;
		display_string(buffer, length, PANEL_X + offset * 10, y, Enemy);
		// offset += length;
	}
}

void if_map(const struct player *restrict players, const struct state *restrict state) // TODO finish this
{
	// clear window
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// display current player's color
	draw_rectangle(PANEL_X - 4, PANEL_Y - 4, PANEL_WIDTH + 8, PANEL_HEIGHT + 8, Player + state->player);
	draw_rectangle(PANEL_X - 3, PANEL_Y - 3, PANEL_WIDTH + 6, PANEL_HEIGHT + 6, Player + state->player);
	draw_rectangle(PANEL_X - 2, PANEL_Y - 2, PANEL_WIDTH + 4, PANEL_HEIGHT + 4, Player + state->player);
	//display_rectangle(0, 0, 256, 16, Player + state->player);

	// Display panel background pattern.
	glColor4ubv(display_colors[White]); // TODO check why is this necessary
	display_image(&image_panel, PANEL_X, PANEL_Y, PANEL_WIDTH, PANEL_HEIGHT);

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
		if (regions[i].owner == state->player) resource_add(&income, &regions[i].income);
		for(slot = regions[i].slots; slot; slot = slot->_next)
			if (slot->player == state->player)
				resource_add(&expenses, &slot->unit->expense);
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

	if (state->region >= 0)
	{
		const struct region *region = regions + state->region;

		// Display flag of the region owner and name of the region.
		display_rectangle(PANEL_X + 4, PANEL_Y + 4, 24, 12, Player + region->owner);
		image_draw(&image_flag, PANEL_X, PANEL_Y);
		display_string(region->name, region->name_length, PANEL_X + image_flag.width + MARGIN, PANEL_Y, Black);

		// Display the slots at the current region.
		if (players[state->player].alliance == players[region->owner].alliance)
		{
			unsigned char position_x[PLAYERS_LIMIT] = {0}, position_y[PLAYERS_LIMIT] = {0};
			unsigned self_count = 0, allies_count = 0;
			for(slot = region->slots; slot; slot = slot->_next)
			{
				if (slot->player == state->player)
				{
					if (!self_count)
					{
						self_count = 1;
						display_rectangle(PANEL_X, SLOT_Y(0) - 3, PANEL_WIDTH, FIELD_SIZE + MARGIN + 16, Self);
					}
				}
				else
				{
					if (!position_y[slot->player])
					{
						allies_count += 1;
						display_rectangle(PANEL_X, SLOT_Y(allies_count) - 3, PANEL_WIDTH, FIELD_SIZE + MARGIN + 16, Ally);
						position_y[slot->player] = allies_count;
					}
				}

				// TODO make this work for more than 6 slots

				x = SLOT_X(position_x[slot->player]);
				y = SLOT_Y(position_y[slot->player]);
				position_x[slot->player] += 1;

				display_unit(slot->unit->index, x, y, Player + slot->player, slot->count);
				if (slot == state->selected.slot) draw_rectangle(x - 1, y - 1, FIELD_SIZE + 2, FIELD_SIZE + 2, White);

				// Draw the destination of each moving slot.
				if ((slot->player == state->player) && (!state->selected.slot || (slot == state->selected.slot)) && (slot->move->index != state->region))
				{
					struct point from = {slot->location->center.x, slot->location->center.y};
					struct point to = {slot->move->center.x, slot->move->center.y};
					display_arrow(from, to, MAP_X, MAP_Y, Self);
				}
			}

			display_rectangle(BUILDING_X(0), BUILDING_Y, 98, 32, White);
			if (region->buildings & BUILDING_IRRIGATION) image_draw(image_buildings + 0, BUILDING_X(0), BUILDING_Y);
			if (region->buildings & BUILDING_LUMBERMILL) image_draw(image_buildings + 1, BUILDING_X(1), BUILDING_Y);
			if (region->buildings & BUILDING_MINE) image_draw(image_buildings + 2, BUILDING_X(2), BUILDING_Y);
		}

		if (state->player == region->owner)
		{
			display_string(S("train:"), PANEL_X + 2, PANEL_Y + 200, Black); // TODO fix y coordinate

			// Display train queue.
			size_t index;
			unsigned progress;
			for(index = 0; index < TRAIN_QUEUE; ++index)
				if (region->train[index])
				{
					if (index) progress = 0;
					else progress = ((double)region->train_time / region->train[index]->time) * FIELD_SIZE;
					display_unit(region->train[index]->index, TRAIN_X(index), TRAIN_Y, White, 0);
					display_rectangle(TRAIN_X(index), TRAIN_Y, FIELD_SIZE, FIELD_SIZE - progress, Progress); // TODO this should show train progress
				}
				else display_rectangle(TRAIN_X(index), TRAIN_Y, FIELD_SIZE, FIELD_SIZE, Black);

			// Display units available for training.
			// TODO use game->units_count
			display_unit(0, INVENTORY_X(0), INVENTORY_Y, Player, 0);
			display_unit(1, INVENTORY_X(1), INVENTORY_Y, Player, 0);
			display_unit(2, INVENTORY_X(2), INVENTORY_Y, Player, 0);
		}
	}

	// Treasury
	// TODO income is no longer properly calculated
	show_resource(S("gold: "), players[state->player].treasury.gold, income.gold, expenses.gold, RESOURCE_GOLD);
	show_resource(S("food: "), players[state->player].treasury.food, income.food, expenses.food, RESOURCE_FOOD);
	show_resource(S("wood: "), players[state->player].treasury.wood, income.wood, expenses.wood, RESOURCE_WOOD);
	show_resource(S("iron: "), players[state->player].treasury.iron, income.iron, expenses.iron, RESOURCE_IRON);
	show_resource(S("stone: "), players[state->player].treasury.stone, income.stone, expenses.stone, RESOURCE_ROCK);

	glFlush();
	glXSwapBuffers(display, drawable);
}

void if_set(struct pawn *bf[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	battlefield = bf;
}

void if_regions(struct game *restrict game)
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
