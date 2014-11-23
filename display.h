#include <GL/gl.h>

#include <X11/Xlib-xcb.h>

struct point 
{ 
	unsigned x, y;
};

struct polygon
{
	size_t vertices;
	struct point points[];
};

struct font
{
	XFontStruct *info;
	unsigned width, height;
	GLuint base;
};

enum color {White, Gray, Black, B0, Progress, Select, Self, Ally, Enemy, Player};
extern unsigned char display_colors[][4]; // TODO remove this

void display_rectangle(unsigned x, unsigned y, unsigned width, unsigned height, enum color color);
void display_polygon(const struct polygon *restrict polygon, int offset_x, int offset_y);

void display_arrow(struct point from, struct point to, int offset_x, int offset_y, enum color color);

int font_init(Display *restrict dpy, struct font *restrict font);

// TODO rename this
#define glFont glListBase

void display_string(const char *string, size_t length, unsigned x, unsigned y, enum color color);
