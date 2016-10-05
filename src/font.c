#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftglyph.h>
#include <GL/gl.h>

#include "errors.h"
#include "draw.h"
#include "font.h"

#define FONT_DPI 72

// TODO refactor the code (rename, etc.) and add comments

// TODO improve return error codes
// TODO cleanup on error

struct font font10, font12, font24;

static inline int next_p2(int a)
{
	int rval = 1;
	while (rval < a)
		rval *= 2;
	return rval;
}

static int bitmap_texture(FT_Bitmap *restrict bitmap, GLuint *restrict texture)
{
	unsigned width = next_p2(bitmap->width);
	unsigned rows = next_p2(bitmap->rows);

	GLubyte *buffer = calloc(rows * width, 2 * sizeof(GLubyte));
	if (!buffer)
		return ERROR_MEMORY;

	for(size_t y = 0; y < rows; y++) {
		for(size_t x = 0; x < width; x++) {
			buffer[2 * (x + y * width)] = 255;
			buffer[2 * (x + y * width) + 1] = (x >= bitmap->width || y >= bitmap->rows) ? 0 : bitmap->buffer[x + bitmap->width * y];
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
	if (FT_New_Face(*library, "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf", 0, &face))
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

		glNewList(font->base + i, GL_COMPILE);
		glBindTexture(GL_TEXTURE_2D, font->textures[i]);

		glPushMatrix();

		// Adjust for texture padding.
		float x = (float)glyph_bitmap->bitmap.width / next_p2(glyph_bitmap->bitmap.width);
		float y = (float)glyph_bitmap->bitmap.rows / next_p2(glyph_bitmap->bitmap.rows);

		glTranslatef(glyph_bitmap->left, size - glyph_bitmap->top, 0);

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

		font->advance[i].x = face->glyph->advance.x / 64;
		font->advance[i].y = face->glyph->advance.y / 64;

		glEndList();
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
