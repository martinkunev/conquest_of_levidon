#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

#include <X11/Xlib-xcb.h>

#define POINT(x, y) (struct point){(x), (y)}
#define POINT_NONE (struct point){-1, -1}

struct point 
{ 
	int x, y;
};

struct box
{
	unsigned width, height;
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

enum color {White, Gray, Black, Error, Unexplored, Progress, Select, Self, Ally, Enemy, PathReachable, PathUnreachable, Hover, FieldReachable, Player};
extern unsigned char display_colors[][4]; // TODO remove this

static inline int point_eq(struct point a, struct point b)
{
	return ((a.x == b.x) && (a.y == b.y));
}

void draw_rectangle(unsigned x, unsigned y, unsigned width, unsigned height, enum color color);
void fill_rectangle(unsigned x, unsigned y, unsigned width, unsigned height, enum color color);

void fill_polygon(const struct polygon *restrict polygon, int offset_x, int offset_y);

void display_arrow(struct point from, struct point to, int offset_x, int offset_y, enum color color);

int font_init(struct font *restrict font, const char *restrict name);
struct box string_box(const char *string, size_t length, struct font *restrict font);
void font_term(struct font *restrict font);

void draw_cursor(const char *string, size_t length, unsigned x, unsigned y, struct font *restrict font, enum color color);
unsigned draw_string(const char *string, size_t length, unsigned x, unsigned y, struct font *restrict font, enum color color);
