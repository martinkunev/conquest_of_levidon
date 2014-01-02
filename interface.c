#include <stdlib.h>

#include <GL/gl.h>
#include <GL/glx.h>

#include <xcb/xcb.h>

#include <X11/Xlib-xcb.h>

#include <png.h>

#include "format.h"
#include "battle.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

// http://xcb.freedesktop.org/opengl/
// http://xcb.freedesktop.org/tutorial/events/
// http://techpubs.sgi.com/library/dynaweb_docs/0640/SGI_Developer/books/OpenGL_Porting/sgi_html/ch04.html

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

#define FIELD_SIZE 32

#define CTRL_X 768
#define CTRL_Y 0

#define MAGIC_NUMBER_SIZE 8

// TODO rename these
#define glFont glListBase
#define glString_(s, l, ...) glCallLists((l), GL_UNSIGNED_BYTE, (s));
#define glString(...) glString_(__VA_ARGS__, sizeof(__VA_ARGS__) - 1)

static Display *display;
static xcb_connection_t *connection;
static xcb_window_t window;
static GLXDrawable drawable;
static GLXContext context;

static struct pawn *(*battlefield)[BATTLEFIELD_WIDTH];

static struct
{
	unsigned char player; // current player
	unsigned char x, y; // current field
	signed char pawn; // number of the pawn in the field
} state;

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
	[Player + 0] = {192, 0, 0, 255},
	[Player + 1] = {255, 255, 0, 255},
	[Player + 2] = {128, 128, 0, 255},
	[Player + 3] = {0, 255, 0, 255},
	[Player + 4] = {0, 255, 255, 255},
	[Player + 5] = {0, 128, 128, 255},
	[Player + 6] = {0, 0, 128, 255},
	[Player + 7] = {255, 0, 255, 255},
	[Player + 8] = {128, 0, 128, 255},
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

static int texture_png(char *filename, GLuint *restrict texture)
{
	int img;
	struct stat info;
	char header[MAGIC_NUMBER_SIZE];

	// TODO fix error handling

	// Open the file for reading
	if ((img = open(filename, O_RDONLY)) < 0)
		return -1;

	// Check file size and file type
	// TODO read() may not read the whole magic
	fstat(img, &info);
	if ((info.st_size < MAGIC_NUMBER_SIZE) || (read(img, header, MAGIC_NUMBER_SIZE) != MAGIC_NUMBER_SIZE) || png_sig_cmp(header, 0, MAGIC_NUMBER_SIZE))
	{
		close(img);
		return -1;
	}

	FILE *img_stream;

	png_structp png_ptr;
	png_infop info_ptr, end_ptr;

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
	if (!png_ptr)
	{
		close(img);
		return -1;
	}
	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
	{
		png_destroy_read_struct(&png_ptr, 0, 0);
		close(img);
		return -1;
	}
	end_ptr = png_create_info_struct(png_ptr); // TODO why is this necessary
	if (!end_ptr)
	{
		png_destroy_read_struct(&png_ptr, &info_ptr, 0);
		close(img);
		return -1;
	}

	// TODO: the i/o is done the stupid way. it must be rewritten
	img_stream = fdopen(img, "r");
	// TODO error check?
	png_init_io(png_ptr, img_stream);

	// Tell libpng that we already read MAGIC_NUMBER_SIZE bytes.
	png_set_sig_bytes(png_ptr, MAGIC_NUMBER_SIZE);

	// Read all the info up to the image data.
	png_read_info(png_ptr, info_ptr);

	// get info about png
	png_uint_32 width, height;
	int bit_depth, color_type;
	png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, 0, 0, 0);

	if (bit_depth != 8) ; // TODO unsupported

	GLint format;
	switch (color_type)
	{
	case PNG_COLOR_TYPE_RGB:
		format = GL_RGB;
		break;
	case PNG_COLOR_TYPE_RGB_ALPHA:
		format = GL_RGBA;
		break;
	default:
		//fprintf(stderr, "%s: Unknown libpng color type %d.\n", file_name, color_type);
		// TODO error
		break;
	}

	// Row size in bytes.
	// glTexImage2d requires rows to be 4-byte aligned
	unsigned rowbytes = png_get_rowbytes(png_ptr, info_ptr);
	rowbytes += 3 - ((rowbytes - 1) % 4);

	png_byte **rows = malloc(height * (sizeof(png_byte *) + rowbytes * sizeof(png_byte)) + 15); // TODO why + 15
	if (!rows)
	{
		fclose(img_stream);
		png_destroy_read_struct(&png_ptr, &info_ptr, &end_ptr);
		close(img);
		return -1;
	}

	// set the individual row_pointers to point at the correct offsets
	size_t i;
	png_byte *image_data = (png_byte *)(rows + height);
	for(i = 0; i < height; i++)
		rows[height - 1 - i] = image_data + i * rowbytes;

	// read the png into image_data through row_pointers
	png_read_image(png_ptr, rows);

	fclose(img_stream);
	png_destroy_read_struct(&png_ptr, &info_ptr, &end_ptr);
	close(img);

	// Generate the OpenGL texture object.
    glGenTextures(1, texture);
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, image_data);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	free(rows);

	return 0;
}

static int if_image(int x, int y, int width, int height, char *filename)
{
	GLuint texture;

	if (texture_png(filename, &texture) < 0) return -1;

	glEnable(GL_TEXTURE_2D);

	glBegin(GL_QUADS);

	glTexCoord2d(0, 0);
	glVertex2f(x + width, y + height);

	glTexCoord2d(1, 0);
	glVertex2f(x, y + height);

	glTexCoord2d(1, 1);
	glVertex2f(x, y);

	glTexCoord2d(0, 1);
	glVertex2f(x + width, y);

	glEnd();

	glDisable(GL_TEXTURE_2D);

	glDeleteTextures(1, &texture);

	return 0;
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

void if_expose(void)
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
	for(y = 0; y < BATTLEFIELD_HEIGHT; y += 1)
		for(x = y % 2; x < BATTLEFIELD_WIDTH; x += 2)
			rectangle(x * FIELD_SIZE, y * FIELD_SIZE, FIELD_SIZE, FIELD_SIZE, B0);

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
		rectangle(state.x * FIELD_SIZE, state.y * FIELD_SIZE, FIELD_SIZE, FIELD_SIZE, Select);

		p = battlefield[state.y][state.x];
		do
		{
			rectangle(CTRL_X + 4 + (FIELD_SIZE + 4) * (position % 7), CTRL_Y + 32, FIELD_SIZE, FIELD_SIZE, Player + p->slot->player);

			glColor4ubv(colors[White]);
			length = format_uint(buffer, p->slot->count) - buffer;
			glRasterPos2i(CTRL_X + 4 + (FIELD_SIZE + 4) * (position % 7) + (32 - (length * 10)) / 2, CTRL_Y + 32 + 32 + 18);
			glString(buffer, length);

			// Show destination of each moving pawn.
			// TODO don't draw at the same place twice
			if ((state.pawn < 0) || (position == state.pawn))
				if ((p->move.x[1] != p->move.x[0]) || (p->move.y[1] != p->move.y[0]))
					if_image(FIELD_SIZE * p->move.x[1], FIELD_SIZE * p->move.y[1], FIELD_SIZE, FIELD_SIZE, "img/move_destination.png");

			position += 1;
		} while (p = p->_next);
	}

	glFlush();
	glXSwapBuffers(display, drawable);
}

int input_player(unsigned char player)
{
	state.player = player;

	// Set current field to a field outside of the board.
	state.x = BATTLEFIELD_WIDTH;
	state.y = BATTLEFIELD_HEIGHT;

	state.pawn = -1;

	if_expose();

	KeySym *keysym;
	int keysyms_per_keycode_return;
	KeySym *input;

	// TODO handle modifier keys
	// TODO handle dead keys
	// TODO the keyboard mappings don't work as expected for different keyboard layouts

	// Initialize keyboard mapping table.
	int min_keycode, max_keycode;
	int keysyms_per_keycode;
	XDisplayKeycodes(display, &min_keycode, &max_keycode);
	KeySym *keymap = XGetKeyboardMapping(display, min_keycode, (max_keycode - min_keycode + 1), &keysyms_per_keycode);
	if (!keymap) return -1;

	xcb_generic_event_t *event;
	xcb_button_release_event_t *mouse;

	while (1)
	{
		event = xcb_wait_for_event(connection);
		// TODO consider using xcb_poll_for_event()
		if (!event) return -1;

		switch (event->response_type & ~0x80)
		{
		case XCB_BUTTON_PRESS:
			mouse = (xcb_button_release_event_t *)event;
			if (mouse->event_x < 768)
			{
				state.x = mouse->event_x / FIELD_SIZE;
				state.y = mouse->event_y / FIELD_SIZE;
				state.pawn = -1;
			}
			else
			{
				if (((CTRL_Y + 32) <= mouse->event_y) && (mouse->event_y < (CTRL_Y + 32 + FIELD_SIZE)) && ((CTRL_X + 4) <= mouse->event_x) && (mouse->event_x < (CTRL_X + 4 + (FIELD_SIZE + 4) * 7)))
					state.pawn = (mouse->event_x - CTRL_X - 4) / (FIELD_SIZE + 4);
				else
					state.pawn = -1;
			}
		case XCB_EXPOSE:
			if_expose();
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

	return;

error:
	XCloseDisplay(display);
}

void if_set(struct pawn *bf[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	battlefield = bf;
}

void if_term(void)
{
	glXDestroyWindow(display, drawable);
	xcb_destroy_window(connection, window);
	glXDestroyContext(display, context);
	XCloseDisplay(display);
}
