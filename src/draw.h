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
#include <GL/gl.h>

#include <X11/Xlib-xcb.h>

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
extern const unsigned char display_colors[][4];

static inline int point_eq(struct point a, struct point b)
{
	return ((a.x == b.x) && (a.y == b.y));
}

void fill_circle(unsigned x, unsigned y, unsigned radius, enum color color);

void draw_rectangle(unsigned x, unsigned y, unsigned width, unsigned height, const unsigned char color[static 4]);
void fill_rectangle(unsigned x, unsigned y, unsigned width, unsigned height, const unsigned char color[static 4]);

void draw_polygon(const struct polygon *restrict polygon, int offset_x, int offset_y, const unsigned char color[static 4]);
void fill_polygon(const struct polygon *restrict polygon, int offset_x, int offset_y, const unsigned char color[static 4]);

void display_arrow(struct point from, struct point to, int offset_x, int offset_y, enum color color);

int font_init(struct font *restrict font, const char *restrict name);
struct box string_box(const char *string, size_t length, struct font *restrict font);
void font_term(struct font *restrict font);

void draw_cursor(const char *string, size_t length, unsigned x, unsigned y, struct font *restrict font, enum color color);
unsigned draw_string(const char *string, size_t length, unsigned x, unsigned y, struct font *restrict font, enum color color);
