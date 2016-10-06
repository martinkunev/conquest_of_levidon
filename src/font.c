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

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftglyph.h>
#include <GL/gl.h>

#include "errors.h"
#include "draw.h"
#include "font.h"

#define TYPEFACE "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf"
#define FONT_DPI 72

// TODO improve return error codes
// TODO cleanup on error

struct font font10, font12, font24;

static inline unsigned topower2(unsigned number)
{
	// Round count up to the next power of 2 that is >= number.
	unsigned result = 1;
	while (result < number)
		result *= 2;
	return result;
}

static int bitmap_texture(FT_Bitmap *restrict bitmap, GLuint *restrict texture)
{
	unsigned width = topower2(bitmap->width);
	unsigned rows = topower2(bitmap->rows);

	GLubyte *buffer = malloc(rows * width * 2 * sizeof(*buffer));
	if (!buffer)
		return ERROR_MEMORY;

	for(size_t y = 0; y < rows; y++) {
		for(size_t x = 0; x < width; x++) {
			size_t index = 2 * (y * width + x);
			buffer[index] = 0xff;
			if ((x < bitmap->width) && (y < bitmap->rows))
				buffer[index + 1] = bitmap->buffer[bitmap->width * y + x];
			else
				buffer[index + 1] = 0;
		}
	}

	glBindTexture(GL_TEXTURE_2D, *texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, rows, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, buffer);

	free(buffer);

	return 0;
}

static int font_load(FT_Library *restrict library, struct font *restrict font, unsigned size)
{
	const size_t count = sizeof(font->textures) / sizeof(*font->textures);

	FT_Face face;
	if (FT_New_Face(*library, TYPEFACE, 0, &face))
		return ERROR_MISSING;
	if (FT_Set_Char_Size(face, size * 64, size * 64, FONT_DPI, FONT_DPI))
		return ERROR_INPUT;

	font->base = glGenLists(count);
	glGenTextures(count, font->textures);
	font->size = size;

	for(size_t i = 0; i < count; ++i)
	{
		if (FT_Load_Glyph(face, FT_Get_Char_Index(face, i), FT_LOAD_DEFAULT))
			return ERROR_MEMORY;

		FT_Glyph glyph;
		if (FT_Get_Glyph(face->glyph, &glyph))
			return ERROR_MEMORY;

		if (FT_Glyph_To_Bitmap(&glyph, ft_render_mode_normal, 0, 1))
			return ERROR_MEMORY;

		FT_BitmapGlyph glyph_bitmap = (FT_BitmapGlyph)glyph;
		if (bitmap_texture(&glyph_bitmap->bitmap, font->textures + i) < 0)
			return ERROR_MEMORY;

		// Calculate glyph size relative to texture size.
		double x = (double)glyph_bitmap->bitmap.width / topower2(glyph_bitmap->bitmap.width);
		double y = (double)glyph_bitmap->bitmap.rows / topower2(glyph_bitmap->bitmap.rows);

		// Create display list for the glyph.
		glNewList(font->base + i, GL_COMPILE);
		{
			glPushMatrix();

			glTranslatef(glyph_bitmap->left, size - glyph_bitmap->top, 0);
			glBindTexture(GL_TEXTURE_2D, font->textures[i]);

			glBegin(GL_QUADS);

			glTexCoord2d(0, 0);
			glVertex2f(0, 0);

			glTexCoord2d(0, y);
			glVertex2f(0, glyph_bitmap->bitmap.rows);

			glTexCoord2d(x, y);
			glVertex2f(glyph_bitmap->bitmap.width, glyph_bitmap->bitmap.rows);

			glTexCoord2d(x, 0);
			glVertex2f(glyph_bitmap->bitmap.width, 0);

			glEnd();

			glPopMatrix();

			glTranslatef(face->glyph->advance.x / 64, 0, 0);
		}
		glEndList();

		font->advance[i].x = face->glyph->advance.x / 64;
		font->advance[i].y = face->glyph->advance.y / 64;
	}

	FT_Done_Face(face);

	return 0;
}

int font_init(void)
{
	FT_Library library;
	int status;

	if (FT_Init_FreeType(&library))
		return ERROR_MEMORY;

	status = font_load(&library, &font10, 10);
	if (status < 0) return status;
	status = font_load(&library, &font12, 12);
	if (status < 0) return status;
	status = font_load(&library, &font24, 24);
	if (status < 0) return status;
	
	FT_Done_FreeType(library);

	return 0;
}

static inline void font_unload(struct font *restrict font)
{
	const size_t count = sizeof(font->textures) / sizeof(*font->textures);
	glDeleteLists(font->base, count);
	glDeleteTextures(count, font->textures);
}

void font_term(void)
{
	font_unload(&font10);
	font_unload(&font12);
	font_unload(&font24);
}

struct box string_box(const char *string, size_t length, struct font *restrict font)
{
	struct box box = {0, font->size};
	while (length--)
	{
		struct advance advance = font->advance[(size_t)string[length]];
		box.width += advance.x;
		if (advance.y > box.height)
			box.height = advance.y;
	}
	return box;
}

void draw_cursor(const char *string, size_t length, unsigned x, unsigned y, struct font *restrict font, enum color color)
{
	// TODO ?make this part of draw_string

	struct box box = string_box(string, length, font);

	glColor4ubv(display_colors[color]);

	glBegin(GL_LINES);
	glVertex2f(x + box.width + 1.5, y);
	glVertex2f(x + box.width + 1.5, y + font->size + 1.0);
	glEnd();
}

unsigned draw_string(const char *string, size_t length, unsigned x, unsigned y, struct font *restrict font, enum color color)
{
	glEnable(GL_TEXTURE_2D);

	glPushMatrix();
	glLoadIdentity();
	glTranslatef(x, y, 0);

	glListBase(font->base);
	glColor4ubv(display_colors[color]);
	glCallLists(length, GL_UNSIGNED_BYTE, string);

	glPopMatrix();

	glDisable(GL_TEXTURE_2D);

	return string_box(string, length, font).width;
}
