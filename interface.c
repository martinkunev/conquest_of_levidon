#define GL_GLEXT_PROTOTYPES

#include <GL/glx.h>
#include <GL/glext.h>

#include <xcb/xcb.h>

#include "types.h"
#include "format.h"
#include "image.h"
#include "draw.h"
#include "interface.h"

// http://xcb.freedesktop.org/opengl/
// http://xcb.freedesktop.org/tutorial/events/
// http://techpubs.sgi.com/library/dynaweb_docs/0640/SGI_Developer/books/OpenGL_Porting/sgi_html/ch04.html
// http://tronche.com/gui/x/xlib/graphics/font-metrics/
// http://www.opengl.org/sdk/docs/man2/

// Use xlsfonts to list the available fonts.
// "-misc-dejavu sans mono-bold-r-normal--12-0-0-0-m-0-ascii-0"

#define S(s) (s), sizeof(s) - 1

// TODO compatibility with OpenGL 2.1 (necessary in MacOS X)
#define glGenFramebuffers(...) glGenFramebuffersEXT(__VA_ARGS__)
#define glGenRenderbuffers(...) glGenRenderbuffersEXT(__VA_ARGS__)
#define glBindFramebuffer(...) glBindFramebufferEXT(__VA_ARGS__)
#define glBindRenderbuffer(...) glBindRenderbufferEXT(__VA_ARGS__)
#define glRenderbufferStorage(...) glRenderbufferStorageEXT(__VA_ARGS__)
#define glFramebufferRenderbuffer(...) glFramebufferRenderbufferEXT(__VA_ARGS__)

#define WM_STATE "_NET_WM_STATE"
#define WM_STATE_FULLSCREEN "_NET_WM_STATE_FULLSCREEN"

static xcb_window_t window;
static GLXContext context;

Display *display;
GLXDrawable drawable;
xcb_screen_t *screen;
xcb_connection_t *connection;
KeySym *keymap;
int keysyms_per_keycode;
int keycode_min, keycode_max;

struct font font12;

unsigned SCREEN_WIDTH, SCREEN_HEIGHT;

int if_init(void)
{
	display = XOpenDisplay(0);
	if (!display) return -1;

	connection = XGetXCBConnection(display);
	if (!connection) goto error;

	int visualID = 0;

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

	// query framebuffer configurations
	GLXFBConfig *fb_configs = 0;
	int num_fb_configs = 0;
	fb_configs = glXGetFBConfigs(display, screen_index, &num_fb_configs);
	if (!fb_configs || num_fb_configs == 0) goto error;

	// select first framebuffer config and query visualID
	GLXFBConfig fb_config = fb_configs[0];
	glXGetFBConfigAttrib(display, fb_config, GLX_VISUAL_ID, &visualID);

	// create OpenGL context
	context = glXCreateNewContext(display, fb_config, GLX_RGBA_TYPE, 0, 1);
	if (!context) goto error;

	// create XID's for colormap and window
	xcb_colormap_t colormap = xcb_generate_id(connection);
	window = xcb_generate_id(connection);

	xcb_create_colormap(connection, XCB_COLORMAP_ALLOC_NONE, colormap, screen->root, visualID);

	uint32_t eventmask = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION;
	uint32_t valuelist[] = {eventmask, colormap, 0};
	uint32_t valuemask = XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;

	// TODO set window parameters
	xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 100, 0, screen->width_in_pixels, screen->height_in_pixels, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, visualID, valuemask, valuelist);

	// NOTE: window must be mapped before glXMakeContextCurrent
	xcb_map_window(connection, window); 

	drawable = glXCreateWindow(display, fb_config, window, 0);

	if (!window) // TODO this should be done earlier?
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

	// TODO handle modifier keys
	// TODO handle dead keys
	// TODO the keyboard mappings don't work as expected for different keyboard layouts

	// Initialize keyboard mapping table.
	XDisplayKeycodes(display, &keycode_min, &keycode_max);
	keymap = XGetKeyboardMapping(display, keycode_min, (keycode_max - keycode_min + 1), &keysyms_per_keycode);
	if (!keymap) goto error; // TODO error

	// enable transparency
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// TODO set window title, border, etc.

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
	SCREEN_WIDTH = screen->width_in_pixels;
	SCREEN_HEIGHT = screen->height_in_pixels;

	glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 1);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// Make the window fullscreen.
	/*{
		XEvent event = {0};
		event.type = ClientMessage;
		event.xclient.window = window;
		event.xclient.message_type = XInternAtom(display, WM_STATE, 0);
		event.xclient.format = 32;
		event.xclient.data.l[0] = 1; // 0 == unset; 1 == set; 2 == toggle
		event.xclient.data.l[1] = XInternAtom(display, WM_STATE_FULLSCREEN, 0);

		//XMapWindow(display, window);
		XSendEvent(display, DefaultRootWindow(display), 0, SubstructureRedirectMask | SubstructureNotifyMask, &event);
		XFlush(display);
	}*/
}

void if_term(void)
{
	XFree(keymap);

	glXDestroyWindow(display, drawable);
	xcb_destroy_window(connection, window);
	glXDestroyContext(display, context);
	XCloseDisplay(display);
}
