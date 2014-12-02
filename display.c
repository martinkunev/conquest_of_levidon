#include <stdlib.h>
#include <math.h>

#define GL_GLEXT_PROTOTYPES

//#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glext.h>

#include <xcb/xcb.h>

//#include <X11/Xlib-xcb.h>

#include "display.h"

struct polygon_draw
{
	const struct point *point;
	enum {Wait, Ear, Straight} class;
	struct polygon_draw *prev, *next;
};

extern struct font font;

unsigned char display_colors[][4] = {
	[White] = {255, 255, 255, 255},
	[Gray] = {128, 128, 128, 255},
	[Black] = {0, 0, 0, 255},
	[B0] = {96, 96, 96, 255},
	[Progress] = {128, 128, 128, 128},
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

static inline long cross_product(int fx, int fy, int sx, int sy)
{
	return (fx * sy - sx * fy);
}

static int in_triangle(struct point p, struct point a, struct point b, struct point c)
{
	// TODO think about equality

	// ab x ap
	int sign0 = cross_product(b.x - a.x, b.y - a.y, p.x - a.x, p.y - a.y) > 0; // TODO can this overflow?

	// bc x bp
	int sign1 = cross_product(c.x - b.x, c.y - b.y, p.x - b.x, p.y - b.y) > 0; // TODO can this overflow?

	// ca x cp
	int sign2 = cross_product(a.x - c.x, a.y - c.y, p.x - c.x, p.y - c.y) > 0; // TODO can this overflow?

	return ((sign0 == sign1) && (sign1 == sign2));
}

static int is_ear(const struct polygon_draw *prev, const struct polygon_draw *curr, const struct polygon_draw *next)
{
	// Check vector product sign in order to find out if the angle between the 3 points is reflex or straight.
	long product = cross_product(prev->point->x - curr->point->x, prev->point->y - curr->point->y, next->point->x - curr->point->x, next->point->y - curr->point->y); // TODO can this overflow?
	if (product < 0) return Wait;
	else if (product == 0) return Straight;

	// Check if there is a vertex in the triangle.
	const struct polygon_draw *item;
	for(item = next->next; item != prev; item = item->next)
		if (in_triangle(*item->point, *prev->point, *curr->point, *next->point))
			return Wait;

	return Ear;
}

void display_rectangle(unsigned x, unsigned y, unsigned width, unsigned height, enum color color)
{
	// http://stackoverflow.com/questions/10040961/opengl-pixel-perfect-2d-drawing

	glColor4ubv(display_colors[color]);

	glBegin(GL_QUADS);
	glVertex2i(x + width, y + height);
	glVertex2i(x + width, y);
	glVertex2i(x, y);
	glVertex2i(x, y + height);
	glEnd();
}

void draw_rectangle(unsigned x, unsigned y, unsigned width, unsigned height, enum color color)
{
	// http://stackoverflow.com/questions/10040961/opengl-pixel-perfect-2d-drawing

	glColor4ubv(display_colors[color]);

	glBegin(GL_LINE_LOOP);
	glVertex2f(x + width - 0.5, y + height - 0.5);
	glVertex2f(x + width - 0.5, y + 0.5);
	glVertex2f(x + 0.5, y + 0.5);
	glVertex2f(x + 0.5, y + height - 0.5);
	glEnd();
}

// Display a region as a polygon, using ear clipping.
void display_polygon(const struct polygon *restrict polygon, int offset_x, int offset_y)
{
	// assert(polygon->vertices > 2);

	size_t vertices_left = polygon->vertices;
	size_t i;

	// Initialize cyclic linked list with the polygon's vertices.
	struct polygon_draw *draw = malloc(vertices_left * sizeof(*draw));
	if (!draw) return; // TODO
	draw[0].point = polygon->points;
	draw[0].prev = draw + vertices_left - 1;
	for(i = 1; i < vertices_left; ++i)
	{
		draw[i].point = polygon->points + i;
		draw[i].prev = draw + i - 1;
		draw[i - 1].next = draw + i;
	}
	draw[vertices_left - 1].next = draw;

	struct polygon_draw *vertex;
	vertex = draw;
	do
	{
		vertex->class = is_ear(vertex->prev, vertex, vertex->next);
		vertex = vertex->next;
	} while (vertex != draw);

	while (vertices_left > 3)
	{
		// find a triangle to draw
		switch (vertex->class)
		{
		case Ear:
			break;

		case Straight:
			vertices_left -= 1;
			vertex->prev->next = vertex->next;
			vertex->next->prev = vertex->prev;
		default:
			vertex = vertex->next;
			continue;
		}

		glBegin(GL_POLYGON);
		glVertex2f(offset_x + vertex->prev->point->x, offset_y + vertex->prev->point->y);
		glVertex2f(offset_x + vertex->point->x, offset_y + vertex->point->y);
		glVertex2f(offset_x + vertex->next->point->x, offset_y + vertex->next->point->y);
		glEnd();

		// clip the triangle from the polygon
		vertices_left -= 1;
		vertex->prev->next = vertex->next;
		vertex->next->prev = vertex->prev;
		vertex = vertex->next;

		// update the class of the neighboring triangles
		vertex->prev->class = is_ear(vertex->prev->prev, vertex->prev, vertex);
		vertex->class = is_ear(vertex->prev, vertex, vertex->next);
	}

	glBegin(GL_POLYGON);
	glVertex2f(offset_x + vertex->prev->point->x, offset_y + vertex->prev->point->y);
	glVertex2f(offset_x + vertex->point->x, offset_y + vertex->point->y);
	glVertex2f(offset_x + vertex->next->point->x, offset_y + vertex->next->point->y);
	glEnd();

	free(draw);
}

#define BACK_RADIUS 2 /* width of the back of the arrow */
#define FRONT_RADIUS 2 /* radius of the front of the arrow (behind the head) */
#define HEAD_RADIUS 5 /* radius of the head of the arrow */
#define HEAD_LENGTH 10 /* length of the head of the arrow */

// TODO rewrite this?
void display_arrow(struct point from, struct point to, int offset_x, int offset_y, enum color color)
{
	int dx = to.x - from.x, dy = to.y - from.y;
	double l = sqrt(dx * dx + dy * dy);

	double angle_y = dy / l, angle_x = dx / l;

	double hx, hy;
	hx = to.x - HEAD_LENGTH * angle_x;
	hy = to.y - HEAD_LENGTH * angle_y;

	glColor4ubv(display_colors[color]);

	glBegin(GL_POLYGON);
	glVertex2f(hx + FRONT_RADIUS * angle_y + offset_x, hy - FRONT_RADIUS * angle_x + offset_y);
	glVertex2f(hx + HEAD_RADIUS * angle_y + offset_x, hy - HEAD_RADIUS * angle_x + offset_y);
	glVertex2f(to.x + offset_x, to.y + offset_y);
	glVertex2f(hx - HEAD_RADIUS * angle_y + offset_x, hy + HEAD_RADIUS * angle_x + offset_y);
	glVertex2f(hx - FRONT_RADIUS * angle_y + offset_x, hy + FRONT_RADIUS * angle_x + offset_y);
	glVertex2f(from.x - BACK_RADIUS * angle_y + offset_x, from.y + BACK_RADIUS * angle_x + offset_y);
	glVertex2f(from.x + BACK_RADIUS * angle_y + offset_x, from.y - BACK_RADIUS * angle_x + offset_y);
	glEnd();
}

int font_init(Display *restrict dpy, struct font *restrict font)
{
	unsigned first, last;

	font->info = XLoadQueryFont(dpy, "-misc-dejavu sans mono-medium-r-normal--0-0-0-0-m-0-ascii-0");
	if (!font->info) return -1;

	first = font->info->min_char_or_byte2;
	last = font->info->max_char_or_byte2;

	font->width = font->info->max_bounds.rbearing - font->info->min_bounds.lbearing;
	font->height = font->info->max_bounds.ascent + font->info->max_bounds.descent;

	font->base = glGenLists(last + 1);
	if (!font->base) return -1;

	glXUseXFont(font->info->fid, first, last - first + 1, font->base + first);
	return 0;
}

void display_string(const char *string, size_t length, unsigned x, unsigned y, enum color color)
{
	glColor4ubv(display_colors[color]);
	glRasterPos2i(x - font.info->min_bounds.lbearing, y + font.info->max_bounds.ascent);
	glCallLists(length, GL_UNSIGNED_BYTE, string);
}
