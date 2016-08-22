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
#include <GL/glx.h>
#include <GL/glext.h>

#include "log.h"
#include "draw.h"
#include "interface.h"

// http://xcb.freedesktop.org/opengl/
// http://xcb.freedesktop.org/tutorial/events/
// http://techpubs.sgi.com/library/dynaweb_docs/0640/SGI_Developer/books/OpenGL_Porting/sgi_html/ch04.html
// http://tronche.com/gui/x/xlib/graphics/font-metrics/
// http://www.opengl.org/sdk/docs/man2/

// Use xlsfonts to list the available fonts.
// "-misc-dejavu sans mono-bold-r-normal--12-0-0-0-m-0-ascii-0"

// TODO compatibility with OpenGL 2.1 (necessary in MacOS X)
#define glGenFramebuffers(...) glGenFramebuffersEXT(__VA_ARGS__)
#define glGenRenderbuffers(...) glGenRenderbuffersEXT(__VA_ARGS__)
#define glBindFramebuffer(...) glBindFramebufferEXT(__VA_ARGS__)
#define glBindRenderbuffer(...) glBindRenderbufferEXT(__VA_ARGS__)
#define glRenderbufferStorage(...) glRenderbufferStorageEXT(__VA_ARGS__)
#define glFramebufferRenderbuffer(...) glFramebufferRenderbufferEXT(__VA_ARGS__)

#define WM_STATE "_NET_WM_STATE"
#define WM_STATE_FULLSCREEN "_NET_WM_STATE_FULLSCREEN"

static xcb_screen_t *screen;
static xcb_window_t window;
static GLXContext context;

Display *display;
GLXDrawable drawable;
xcb_connection_t *connection;
KeySym *keymap;
int keysyms_per_keycode;
int keycode_min, keycode_max;

struct font font9, font12, font24;

unsigned WINDOW_WIDTH, WINDOW_HEIGHT;

int if_init(void)
{
	display = XOpenDisplay(0);
	if (!display) return -1;

	connection = XGetXCBConnection(display);
	if (!connection) goto error;

	XSetEventQueueOwner(display, XCBOwnsEventQueue);

	// find XCB screen
	// TODO better handling for multiple screens
	int screen_index = XDefaultScreen(display);
	xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(xcb_get_setup(connection));
	int offset = screen_index;
	while (offset)
	{
		if (!iterator.rem) goto error; // TODO
		xcb_screen_next(&iterator);
		offset -= 1;
	}
	screen = iterator.data;

	// Choose a framebuffer configuration.
	const int attributes[] = {GLX_BUFFER_SIZE, 32, GLX_DEPTH_SIZE, 24, GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_DOUBLEBUFFER, True, None};
	int fb_configs_count = 0;
	GLXFBConfig *fb_configs = glXChooseFBConfig(display, screen_index, attributes, &fb_configs_count);
	if (!fb_configs || (fb_configs_count == 0)) goto error;
	GLXFBConfig fb_config = fb_configs[0];

	// create XID's for colormap and window
	xcb_colormap_t colormap = xcb_generate_id(connection);
	window = xcb_generate_id(connection);

	// config and query visual_id
	int visual_id = 0;
	glXGetFBConfigAttrib(display, fb_config, GLX_VISUAL_ID, &visual_id);
	xcb_create_colormap(connection, XCB_COLORMAP_ALLOC_NONE, colormap, screen->root, visual_id);

	uint32_t eventmask = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION;
	uint32_t valuelist[] = {eventmask, colormap, 0};
	uint32_t valuemask = XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;

	// Create the window and make it fullscreen.
	{
		XWindowAttributes attributes;
		XGetWindowAttributes(display, screen->root, &attributes);
		WINDOW_WIDTH = attributes.width;
		WINDOW_HEIGHT = attributes.height;

		XEvent event = {0};
		event.type = ClientMessage;
		event.xclient.window = window;
		event.xclient.message_type = XInternAtom(display, WM_STATE, 0);
		event.xclient.format = 32;
		event.xclient.data.l[0] = 1; // 0 == unset; 1 == set; 2 == toggle
		event.xclient.data.l[1] = XInternAtom(display, WM_STATE_FULLSCREEN, 0);

		// TODO set window parameters
		// TODO set window title, border, etc.
		xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, visual_id, valuemask, valuelist);
		xcb_map_window(connection, window); // NOTE: window must be mapped before glXMakeContextCurrent
		XSendEvent(display, DefaultRootWindow(display), 0, SubstructureRedirectMask | SubstructureNotifyMask, &event);
		XFlush(display);
	}

	drawable = glXCreateWindow(display, fb_config, window, 0);

	// Create OpenGL context and make it current.
	context = glXCreateNewContext(display, fb_config, GLX_RGBA_TYPE, 0, 1);
	if (!context)
	{
		xcb_destroy_window(connection, window);
		goto error;
	}
	if (!glXMakeContextCurrent(display, drawable, drawable, context))
	{
		glXDestroyContext(display, context);
		xcb_destroy_window(connection, window);
		goto error;
	}

	// TODO use DPI-based font size
	// https://wiki.archlinux.org/index.php/X_Logical_Font_Description
	if ((font_init(&font9, "-*-dejavu sans-bold-r-normal--9-*-*-*-p-0-ascii-0") < 0) && (font_init(&font9, "-*-*-bold-r-normal--9-*-*-*-p-0-ascii-0") < 0))
	{
		xcb_destroy_window(connection, window);
		glXDestroyContext(display, context);
		LOG_ERROR("Cannot load font with size 9");
		goto error;
	}
	if ((font_init(&font12, "-*-dejavu sans-bold-r-normal--12-*-*-*-p-0-ascii-0") < 0) && (font_init(&font12, "-*-*-bold-r-normal--12-*-*-*-p-0-ascii-0") < 0))
	{
		font_term(&font9);
		xcb_destroy_window(connection, window);
		glXDestroyContext(display, context);
		LOG_ERROR("Cannot load font with size 12");
		goto error;
	}
	if ((font_init(&font24, "-*-dejavu sans-bold-r-normal--24-*-*-*-p-0-ascii-0") < 0) && (font_init(&font24, "-*-*-bold-r-normal--24-*-*-*-p-0-ascii-0") < 0))
	{
		font_term(&font9);
		font_term(&font12);
		xcb_destroy_window(connection, window);
		glXDestroyContext(display, context);
		LOG_ERROR("Cannot load font with size 24");
		goto error;
	}

	// TODO handle modifier keys
	// TODO handle dead keys
	// TODO the keyboard mappings don't work as expected for different keyboard layouts

	// Initialize keyboard mapping table.
	XDisplayKeycodes(display, &keycode_min, &keycode_max);
	keymap = XGetKeyboardMapping(display, keycode_min, (keycode_max - keycode_min + 1), &keysyms_per_keycode);
	if (!keymap)
	{
		font_term(&font24);
		font_term(&font12);
		font_term(&font9);
		goto error; // TODO error
	}

	// enable transparency
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glClearColor(0.0, 0.0, 0.0, 0.0);

	return 0;

error:
	// TODO free what there is to be freed
	XCloseDisplay(display);
	return -1;
}

// TODO ? call this after resize
void if_display(void)
{
	glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, 0, 1);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void input_display(void (*if_display)(const void *, const struct game *), const struct game *restrict game, void *state)
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	if_display(state, game);
	//glFlush();
	glXSwapBuffers(display, drawable);
}

void input_display_timer(void (*if_display)(const void *, const struct game *, double), const struct game *restrict game, double progress, void *state)
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	if_display(state, game, progress);
	//glFlush();
	glXSwapBuffers(display, drawable);
	glFinish();
}

void if_term(void)
{
	XFree(keymap);
	font_term(&font24);
	font_term(&font12);
	font_term(&font9);

	glXDestroyWindow(display, drawable);
	xcb_destroy_window(connection, window);
	glXDestroyContext(display, context);
	XCloseDisplay(display);
}
