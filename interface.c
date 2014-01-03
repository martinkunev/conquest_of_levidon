#include <stdlib.h>

#include <GL/gl.h>
#include <GL/glx.h>

#include <xcb/xcb.h>

#include <X11/Xlib-xcb.h>

#include "format.h"
#include "battle.h"
#include "image.h"
#include "interface.h"

// http://xcb.freedesktop.org/opengl/
// http://xcb.freedesktop.org/tutorial/events/
// http://techpubs.sgi.com/library/dynaweb_docs/0640/SGI_Developer/books/OpenGL_Porting/sgi_html/ch04.html
// http://open.gl

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

#define REGION_SIZE 48
#define FIELD_SIZE 32

#define CTRL_X 768
#define CTRL_Y 0

#define PAWN_MARGIN 4

#define STRING(s) (s), sizeof(s) - 1

// TODO rename these
#define glFont glListBase
#define glString_(s, l, ...) glCallLists((l), GL_UNSIGNED_BYTE, (s));
#define glString(...) glString_(__VA_ARGS__, sizeof(__VA_ARGS__) - 1)

// TODO add checks like the one in input_train() to make sure a player can only act on its own regions

static Display *display;
static xcb_connection_t *connection;
static xcb_window_t window;
static GLXDrawable drawable;
static GLXContext context;

static struct pawn *(*battlefield)[BATTLEFIELD_WIDTH];
static struct region (*regions)[MAP_HEIGHT];

static struct image image_move_destination, image_selected, image_flag;

static struct
{
	unsigned char player; // current player
	unsigned char x, y; // current field
	struct pawn *pawn;
	signed char pawn_index;
} state;

struct area
{
	unsigned left, right, top, bottom;
	void (*callback)(const xcb_button_release_event_t *restrict, unsigned, unsigned, const struct player *restrict);
};

// Create a struct that stores all the information about the battle (battlefield, players, etc.)

enum {White, Gray, Black, B0, Select, Self, Ally, Enemy, Player};
static unsigned char colors[][4] = {
	[White] = {192, 192, 192, 255},
	[Gray] = {128, 128, 128, 255},
	[Black] = {64, 64, 64, 255},
	[B0] = {96, 96, 96, 255},
	[Select] = {255, 255, 255, 96},
	[Self] = {0, 192, 0, 255},
	[Ally] = {0, 0, 255, 255},
	[Enemy] = {255, 0, 0, 255},
	[Player + 0] = {192, 192, 192, 255},
	[Player + 1] = {255, 255, 0, 255},
	[Player + 2] = {128, 128, 0, 255},
	[Player + 3] = {0, 255, 0, 255},
	[Player + 4] = {0, 255, 255, 255},
	[Player + 5] = {0, 128, 128, 255},
	[Player + 6] = {0, 0, 128, 255},
	[Player + 7] = {255, 0, 255, 255},
	[Player + 8] = {128, 0, 128, 255},
	[Player + 9] = {192, 0, 0, 255},
};

struct font
{
	XFontStruct *info;
	GLuint base;
};

static int font_init(Display *dpy, struct font *restrict font)
{
	unsigned int first, last;

	font->info = XLoadQueryFont(dpy, "-misc-dejavu sans mono-medium-r-normal--0-0-0-0-m-0-ascii-0");
	if (!font->info) return -1;

	first = font->info->min_char_or_byte2;
	last = font->info->max_char_or_byte2;

	font->base = glGenLists(last + 1);
	if (!font->base) return -1;

	glXUseXFont(font->info->fid, first, last - first + 1, font->base + first);
	return 0;
}

static void rectangle(unsigned x, unsigned y, unsigned width, unsigned height, int color)
{
	glColor4ubv(colors[color]);

	// TODO why not width - 1 and height - 1
	glBegin(GL_QUADS);
	glVertex2f(x, y);
	glVertex2f(x + width, y);
	glVertex2f(x + width, y + height);
	glVertex2f(x, y + height);
	glEnd();
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

	// Create OpenGL context
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

	struct font f;
	font_init(display, &f); // TODO error check
	glFont(f.base);

	// enable transparency
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// TODO set window title, border, etc.

	// TODO full screen

	if_reshape(SCREEN_WIDTH, SCREEN_HEIGHT); // TODO call this after resize

	image_load_png(&image_move_destination, "img/move_destination.png");
	image_load_png(&image_selected, "img/selected.png");
	image_load_png(&image_flag, "img/flag.png");

	return;

error:
	XCloseDisplay(display);
}

void if_expose(const struct player *restrict players)
{
	// clear window
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// show the right panel in gray
	rectangle(768, 0, 256, 768, Gray);

	// draw rectangle with current player's color
	rectangle(768, 0, 256, 16, Player + state.player);

	size_t x, y;
	struct pawn *p;

	// Battlefield

	// color every other field in white
	/*for(y = 0; y < BATTLEFIELD_HEIGHT; y += 1)
		for(x = y % 2; x < BATTLEFIELD_WIDTH; x += 2)
			rectangle(x * FIELD_SIZE, y * FIELD_SIZE, FIELD_SIZE, FIELD_SIZE, White);*/
	rectangle(0, 0, BATTLEFIELD_WIDTH * FIELD_SIZE, BATTLEFIELD_HEIGHT * FIELD_SIZE, B0);

	// display pawns
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			if (battlefield[y][x])
			{
				p = battlefield[y][x];
				do
				{
					rectangle(x * FIELD_SIZE + 2 + (p->slot->player % 3) * 10, y * FIELD_SIZE + 2 + (p->slot->player / 3) * 10, 8, 8, Player + p->slot->player);
				} while (p = p->_next);
			}
		}

	// Display information about the selected field.
	// TODO support more than 7 units on a single field
	char buffer[16];
	size_t length;
	unsigned position = 0;
	unsigned count;
	if ((state.x < BATTLEFIELD_WIDTH) && (state.y < BATTLEFIELD_HEIGHT) && battlefield[state.y][state.x])
	{
		image_draw(&image_selected, state.x * FIELD_SIZE, state.y * FIELD_SIZE);

		p = battlefield[state.y][state.x];
		do
		{
			rectangle(CTRL_X + 4 + (FIELD_SIZE + 4) * (position % 7), CTRL_Y + 32, FIELD_SIZE, FIELD_SIZE, Player + p->slot->player);
			if (position == state.pawn_index) image_draw(&image_selected, CTRL_X + 4 + (FIELD_SIZE + 4) * (position % 7), CTRL_Y + 32);

			glColor4ubv(colors[White]);
			length = format_uint(buffer, p->slot->count) - buffer;
			glRasterPos2i(CTRL_X + 4 + (FIELD_SIZE + 4) * (position % 7) + (32 - (length * 10)) / 2, CTRL_Y + 32 + 32 + 18);
			glString(buffer, length);

			// Show destination of each moving pawn.
			// TODO don't draw at the same place twice
			if ((state.pawn_index < 0) || (position == state.pawn_index))
				if (players[p->slot->player].alliance == players[state.player].alliance)
					if ((p->move.x[1] != p->move.x[0]) || (p->move.y[1] != p->move.y[0]))
						image_draw(&image_move_destination, p->move.x[1] * FIELD_SIZE, p->move.y[1] * FIELD_SIZE);

			position += 1;
		} while (p = p->_next);
	}

	glFlush();
	glXSwapBuffers(display, drawable);
}

void if_map(const struct player *restrict players) // TODO finish this
{
	// clear window
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// show the right panel in gray
	rectangle(768, 0, 256, 768, Gray);

	// draw rectangle with current player's color
	rectangle(768, 0, 256, 16, Player + state.player);

	size_t x, y;
	struct pawn *p;

	char buffer[16];
	size_t length;

	// Map

	for(y = 0; y < MAP_HEIGHT; y += 1)
		for(x = 0; x < MAP_WIDTH; x += 1)
			if (regions[y][x].owner)
			{
				rectangle(x * REGION_SIZE, y * REGION_SIZE, REGION_SIZE, REGION_SIZE, Player + regions[y][x].owner);
				image_draw(&image_flag, x * REGION_SIZE, y * REGION_SIZE);
			}

	if ((state.x < MAP_WIDTH) && (state.y < MAP_HEIGHT))
	{
		const struct region *region = regions[state.y] + state.x;

		glColor4ubv(colors[White]);
		glRasterPos2i(CTRL_X + 16, 46);
		glString(STRING("owner:"));

		rectangle(CTRL_X + 16 + 7 * 10, 32, 16, 16, Player + region->owner);

		if (state.player == region->owner)
		{
			// Display train queue.
			size_t index;
			for(index = 0; index < TRAIN_QUEUE; ++index)
				if (region->train[index]) rectangle(CTRL_X + 16 + ((FIELD_SIZE + 4) * index), 64, FIELD_SIZE, FIELD_SIZE, Player + region->owner);
				else break;

			// Display units available for training.
			rectangle(CTRL_X + 16, 128, FIELD_SIZE, FIELD_SIZE, Player + region->owner);
		}

		if (players[state.player].alliance == players[region->owner].alliance)
		{
			// TODO make this work for more than 7 units
			size_t position = 0;
			const struct slot *slot;
			for(slot = region->slots; slot; slot = slot->_next)
				rectangle(CTRL_X + 4 + ((FIELD_SIZE + 4) * position++), 256, FIELD_SIZE, FIELD_SIZE, Player + slot->player);
		}
	}

	// Treasury

	glColor4ubv(colors[White]);

	length = format_uint(format_bytes(buffer, STRING("gold: ")), players[state.player].treasury.gold) - buffer;
	glRasterPos2i(CTRL_X + 16, 672);
	glString(buffer, length);

	length = format_uint(format_bytes(buffer, STRING("food: ")), players[state.player].treasury.food) - buffer;
	glRasterPos2i(CTRL_X + 16, 692);
	glString(buffer, length);

	length = format_uint(format_bytes(buffer, STRING("wood: ")), players[state.player].treasury.wood) - buffer;
	glRasterPos2i(CTRL_X + 16, 712);
	glString(buffer, length);

	length = format_uint(format_bytes(buffer, STRING("iron: ")), players[state.player].treasury.iron) - buffer;
	glRasterPos2i(CTRL_X + 16, 732);
	glString(buffer, length);

	length = format_uint(format_bytes(buffer, STRING("rock: ")), players[state.player].treasury.rock) - buffer;
	glRasterPos2i(CTRL_X + 16, 752);
	glString(buffer, length);

	glFlush();
	glXSwapBuffers(display, drawable);
}

void if_set(struct pawn *bf[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	battlefield = bf;
}

void if_regions(struct region reg[MAP_HEIGHT][MAP_WIDTH])
{
	regions = reg;
}

void if_term(void)
{
	glXDestroyWindow(display, drawable);
	xcb_destroy_window(connection, window);
	glXDestroyContext(display, context);
	XCloseDisplay(display);
}

static int input_in(xcb_button_release_event_t *restrict mouse, const struct area *restrict area)
{
	return ((area->left <= mouse->event_x) && (mouse->event_x <= area->right) && (area->top <= mouse->event_y) && (mouse->event_y <= area->bottom));
}

static void input_pawn(const xcb_button_release_event_t *restrict mouse, unsigned x, unsigned y, const struct player *restrict players)
{
	if (!state.pawn) return;

	if (mouse->detail == 1)
	{
		if ((x % (FIELD_SIZE + PAWN_MARGIN)) >= FIELD_SIZE) goto reset;

		// Select the clicked pawn.
		int position = x / (FIELD_SIZE + PAWN_MARGIN);
		int diff;
		if (state.pawn_index < 0) state.pawn_index = 0;
		while (diff = position - state.pawn_index)
		{
			if (diff > 0)
			{
				if (!state.pawn->_next) goto reset;
				state.pawn = state.pawn->_next;
				state.pawn_index += 1;
			}
			else
			{
				state.pawn = state.pawn->_prev;
				state.pawn_index -= 1;
			}
		}
	}

	return;

reset:

	// Make sure no pawn is selected.
	state.pawn_index = -1;
	while (state.pawn->_prev) state.pawn = state.pawn->_prev;
}

static void input_field(const xcb_button_release_event_t *restrict mouse, unsigned x, unsigned y, const struct player *restrict players)
{
	x /= FIELD_SIZE;
	y /= FIELD_SIZE;

	if (mouse->detail == 1)
	{
		// Set current field.
		state.x = x;
		state.y = y;
		state.pawn_index = -1;
		state.pawn = battlefield[state.y][state.x];
	}
	else if (mouse->detail == 3)
	{
		if (state.pawn_index >= 0)
		{
			// Set the move destination of the selected pawn.
			if ((state.player == state.pawn->slot->player) && reachable(players, battlefield, state.pawn, x, y))
			{
				state.pawn->move.x[1] = x;
				state.pawn->move.y[1] = y;
			}
		}
		else
		{
			// Set the move destination of all pawns on the field.
			struct pawn *pawn = state.pawn;
			while (pawn)
			{
				if ((state.player == pawn->slot->player) && reachable(players, battlefield, pawn, x, y))
				{
					pawn->move.x[1] = x;
					pawn->move.y[1] = y;
				}
				pawn = pawn->_next;
			}
		}
	}
}

static void input_region(const xcb_button_release_event_t *restrict mouse, unsigned x, unsigned y, const struct player *restrict players)
{
	x /= REGION_SIZE;
	y /= REGION_SIZE;

	if (mouse->detail == 1)
	{
		// Set current field.
		state.x = x;
		state.y = y;
	}
	/*else if (mouse->detail == 3)
	{
		if (state.pawn_index >= 0)
		{
			// Set the move destination of the selected pawn.
			if ((state.player == state.pawn->slot->player) && reachable(players, battlefield, state.pawn, x, y))
			{
				state.pawn->move.x[1] = x;
				state.pawn->move.y[1] = y;
			}
		}
		else
		{
			// Set the move destination of all pawns on the field.
			struct pawn *pawn = state.pawn;
			while (pawn)
			{
				if ((state.player == pawn->slot->player) && reachable(players, battlefield, pawn, x, y))
				{
					pawn->move.x[1] = x;
					pawn->move.y[1] = y;
				}
				pawn = pawn->_next;
			}
		}
	}*/
}

static void input_train(const xcb_button_release_event_t *restrict mouse, unsigned x, unsigned y, const struct player *restrict players)
{
	if (state.player != regions[state.y][state.x].owner) return;

	if (mouse->detail == 1)
	{
		struct unit **train = regions[state.y][state.x].train;
		size_t index;
		for(index = 0; index < TRAIN_QUEUE; ++index)
			if (!train[index])
			{
				train[index] = &peasant;
				break;
			}
	}
}

int input_map(unsigned char player, const struct player *restrict players)
{
	state.player = player;

	// Set current field to a field outside of the board.
	state.x = MAP_WIDTH;
	state.y = MAP_HEIGHT;

	if_map(players);

	// TODO handle modifier keys
	// TODO handle dead keys
	// TODO the keyboard mappings don't work as expected for different keyboard layouts

	// Initialize keyboard mapping table.
	// TODO do this just once
	int min_keycode, max_keycode;
	int keysyms_per_keycode;
	XDisplayKeycodes(display, &min_keycode, &max_keycode);
	KeySym *input, *keymap = XGetKeyboardMapping(display, min_keycode, (max_keycode - min_keycode + 1), &keysyms_per_keycode);
	if (!keymap) return -1;

	xcb_generic_event_t *event;
	xcb_button_release_event_t *mouse;

	struct area areas[] = {
		{.left = 0, .right = MAP_WIDTH * REGION_SIZE - 1, .top = 0, .bottom = MAP_HEIGHT * REGION_SIZE - 1, .callback = input_region},
		{.left = CTRL_X + 16, .right = CTRL_X + 16 + FIELD_SIZE - 1, .top = 128, .bottom = 128 + FIELD_SIZE - 1, .callback = input_train}
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
			input = keymap + (((xcb_key_press_event_t *)event)->detail - min_keycode) * keysyms_per_keycode;

			if (*input == 'q')
			{
				free(event);
				XFree(keymap);
				return -1;
			}
			else if (*input == 'n')
			{
				free(event);
				XFree(keymap);
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

int input_player(unsigned char player, const struct player *restrict players)
{
	state.player = player;

	// Set current field to a field outside of the board.
	state.x = BATTLEFIELD_WIDTH;
	state.y = BATTLEFIELD_HEIGHT;

	state.pawn_index = -1;
	state.pawn = 0;

	if_expose(players);

	// TODO handle modifier keys
	// TODO handle dead keys
	// TODO the keyboard mappings don't work as expected for different keyboard layouts

	// Initialize keyboard mapping table.
	// TODO do this just once
	int min_keycode, max_keycode;
	int keysyms_per_keycode;
	XDisplayKeycodes(display, &min_keycode, &max_keycode);
	KeySym *input, *keymap = XGetKeyboardMapping(display, min_keycode, (max_keycode - min_keycode + 1), &keysyms_per_keycode);
	if (!keymap) return -1;

	xcb_generic_event_t *event;
	xcb_button_release_event_t *mouse;

	struct area fields = {.left = 0, .right = BATTLEFIELD_WIDTH * FIELD_SIZE - 1, .top = 0, .bottom = BATTLEFIELD_HEIGHT * FIELD_SIZE - 1};
	struct area pawns = {.left = CTRL_X + 4, .right = CTRL_X + 4 + 7 * (FIELD_SIZE + 4) - 5, .top = CTRL_Y + 32, .bottom = CTRL_Y + 32 + FIELD_SIZE - 1};

	while (1)
	{
		event = xcb_wait_for_event(connection);
		// TODO consider using xcb_poll_for_event()
		if (!event) return -1;

		switch (event->response_type & ~0x80)
		{
		case XCB_BUTTON_PRESS:
			mouse = (xcb_button_release_event_t *)event;
			if (input_in(mouse, &fields)) input_field(mouse, mouse->event_x - fields.left, mouse->event_y - fields.top, players);
			else if (input_in(mouse, &pawns)) input_pawn(mouse, mouse->event_x - pawns.left, mouse->event_y - pawns.top, players);
		case XCB_EXPOSE:
			if_expose(players);
			break;

		case XCB_KEY_PRESS:
			input = keymap + (((xcb_key_press_event_t *)event)->detail - min_keycode) * keysyms_per_keycode;

			//printf("%d %c %c %c %c\n", (int)*input, (int)input[0], (int)input[1], (int)input[2], (int)input[3]);

			if (*input == 'q')
			{
				free(event);
				XFree(keymap);
				return -1;
			}
			else if (*input == 'n')
			{
				free(event);
				XFree(keymap);
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
