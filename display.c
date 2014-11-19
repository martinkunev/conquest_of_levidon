#include <stdlib.h>

#define GL_GLEXT_PROTOTYPES

#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glext.h>

#include "map.h"

struct polygon_draw
{
	const struct point *point;
	enum {Wait, Ear, Straight} class;
	struct polygon_draw *prev, *next;
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

// Display a region as a polygon, using ear clipping.
void display_region(const struct polygon *restrict polygon, int offset_x, int offset_y)
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