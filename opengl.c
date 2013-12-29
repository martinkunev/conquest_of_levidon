#include <GL/gl.h>
#include <GL/glut.h>
#include <GL/glx.h>

#include <X11/Xlib.h>

#include "format.h"

// http://techpubs.sgi.com/library/dynaweb_docs/0640/SGI_Developer/books/OpenGL_Porting/sgi_html/ch04.html

#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 768

#define BATTLEFIELD_WIDTH 24
#define BATTLEFIELD_HEIGHT 24

#define FIELD_SIZE 32

#define CTRL_X 768
#define CTRL_Y 0

// TODO rename these
#define glFont glListBase
#define glString_(s, l, ...) glCallLists((l), GL_UNSIGNED_BYTE, (s));
#define glString(...) glString_(__VA_ARGS__, sizeof(__VA_ARGS__) - 1)

// TODO don't use glut

enum {White, Gray, Black, Self, Ally, Enemy, Player0, Player1, Player2};

static unsigned char colors[][4] = {
	[White] = {192, 192, 192, 255},
	[Gray] = {128, 128, 128, 255},
	[Black] = {64, 64, 64, 255},
	[Self] = {0, 192, 0, 255},
	[Ally] = {0, 0, 192, 255},
	[Enemy] = {255, 0, 0, 255},
	[Player0] = {192, 192, 0, 255},
	[Player1] = {0, 192, 192, 255},
	[Player2] = {192, 0, 255, 255},
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

void if_reshape(int width, int height)
{
	glViewport(0, 0, width, height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, width, height, 0, 0, 1);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void rectangle(unsigned x, unsigned y, unsigned width, unsigned height, int color)
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

void if_display(void)
{
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	rectangle(768, 0, 256, 768, Gray);

	size_t x, y;
	for(y = 0; y < BATTLEFIELD_HEIGHT; y += 1)
		for(x = y % 2; x < BATTLEFIELD_WIDTH; x += 2)
			rectangle(x * FIELD_SIZE, y * FIELD_SIZE, FIELD_SIZE, FIELD_SIZE, White);

	rectangle(CTRL_X + 4 + (FIELD_SIZE + 4) * 0, CTRL_Y + 32, FIELD_SIZE, FIELD_SIZE, Self);
	rectangle(CTRL_X + 4 + (FIELD_SIZE + 4) * 1, CTRL_Y + 32, FIELD_SIZE, FIELD_SIZE, Ally);
	rectangle(CTRL_X + 4 + (FIELD_SIZE + 4) * 2, CTRL_Y + 32, FIELD_SIZE, FIELD_SIZE, Enemy);
	rectangle(CTRL_X + 4 + (FIELD_SIZE + 4) * 3, CTRL_Y + 32, FIELD_SIZE, FIELD_SIZE, Gray);
	rectangle(CTRL_X + 4 + (FIELD_SIZE + 4) * 4, CTRL_Y + 32, FIELD_SIZE, FIELD_SIZE, Player0);
	rectangle(CTRL_X + 4 + (FIELD_SIZE + 4) * 5, CTRL_Y + 32, FIELD_SIZE, FIELD_SIZE, Player1);
	rectangle(CTRL_X + 4 + (FIELD_SIZE + 4) * 6, CTRL_Y + 32, FIELD_SIZE, FIELD_SIZE, Player2);

	unsigned count = 16;

	char buffer[16];
	size_t length = format_uint(buffer, count) - buffer;
	unsigned offset = (32 - (length * 10)) / 2;

	glColor4ubv(colors[White]);
	glRasterPos2i(CTRL_X + 4 + offset, CTRL_Y + 32 + 32 + 18);
	glString(buffer, length);

	glFlush();
	glutSwapBuffers();
}

int main(int argc, char *argv[])
{
	glutInitWindowSize(SCREEN_WIDTH, SCREEN_HEIGHT);
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);

	int window = glutCreateWindow("test");
	// TODO: remove this comment
	//glutFullScreen();

	struct font f;

	Display *d = XOpenDisplay(0);
	font_init(d, &f); // TODO error check
	glFont(f.base);

	glutReshapeFunc(if_reshape); // TODO look at this
	glutDisplayFunc(if_display);

	// Initialize event handlers
	//glutKeyboardFunc(io_keyboard);
	//glutMouseFunc(io_mouse);

	//glutIdleFunc(timer);

	// Enable transparency
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	//io_prepare();

	//game_play();

	glutMainLoop();

	return 0;
}
