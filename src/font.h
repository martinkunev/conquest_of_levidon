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

#define GLYPHS_COUNT 128

struct box
{
	unsigned width, height;
};

struct font
{
	GLuint textures[GLYPHS_COUNT];
	GLuint base;

	struct advance
	{
		unsigned char x, y;
	} advance[GLYPHS_COUNT];

	unsigned size;
};

extern struct font font10, font12, font24;

struct box string_box(const char *string, size_t length, struct font *restrict font);

void draw_cursor(const char *string, size_t length, unsigned x, unsigned y, struct font *restrict font, enum color color);
unsigned draw_string(const char *string, size_t length, unsigned x, unsigned y, struct font *restrict font, enum color color);

int font_init(void);
void font_term(void);
