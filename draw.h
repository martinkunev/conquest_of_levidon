#include <GL/gl.h>

#include <X11/Xlib-xcb.h>

#define POINT(x, y) (struct point){(x), (y)}
#define POINT_NONE (struct point){-1, -1}

struct point 
{ 
	int x, y;
};

struct polygon
{
	size_t vertices_count;
	struct point points[];
};

struct font
{
	XFontStruct *info;
	unsigned width, height;
	GLuint base;
};

struct box
{
	unsigned width, height;
};

enum color {White, Gray, Black, Unexplored, Progress, Select, Self, Ally, Enemy, PathReachable, PathUnreachable, Hover, FieldReachable, Player};
extern unsigned char display_colors[][4]; // TODO remove this

void draw_rectangle(unsigned x, unsigned y, unsigned width, unsigned height, enum color color);

void display_rectangle(unsigned x, unsigned y, unsigned width, unsigned height, enum color color);
void display_polygon(const struct polygon *restrict polygon, int offset_x, int offset_y);

void display_arrow(struct point from, struct point to, int offset_x, int offset_y, enum color color);

int font_init(Display *restrict display, struct font *restrict font, const char *restrict name);
//void font_term(Display *restrict display, struct font *restrict font);

unsigned display_string(const char *string, size_t length, unsigned x, unsigned y, struct font *restrict font, enum color color);
