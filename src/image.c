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

#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <png.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

#include "image.h"

#define MAGIC_NUMBER_SIZE 8

int image_load_png(struct image *restrict image, const char *restrict filename, void (*modify)(const struct image *restrict, png_byte **))
{
	int img;
	struct stat info;
	char header[MAGIC_NUMBER_SIZE];

	// TODO fix error handling

	// Open the file for reading
	if ((img = open(filename, O_RDONLY)) < 0)
		return -1;

	// Check file size and file type
	// TODO read() may not read the whole magic
	fstat(img, &info);
	if ((info.st_size < MAGIC_NUMBER_SIZE) || (read(img, header, MAGIC_NUMBER_SIZE) != MAGIC_NUMBER_SIZE) || png_sig_cmp(header, 0, MAGIC_NUMBER_SIZE))
	{
		close(img);
		return -1;
	}

	FILE *img_stream;

	png_structp png_ptr;
	png_infop info_ptr, end_ptr;

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
	if (!png_ptr)
	{
		close(img);
		return -1;
	}
	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
	{
		png_destroy_read_struct(&png_ptr, 0, 0);
		close(img);
		return -1;
	}
	end_ptr = png_create_info_struct(png_ptr); // TODO why is this necessary
	if (!end_ptr)
	{
		png_destroy_read_struct(&png_ptr, &info_ptr, 0);
		close(img);
		return -1;
	}

	// TODO: the i/o is done the stupid way. it must be rewritten
	img_stream = fdopen(img, "r");
	// TODO error check?
	png_init_io(png_ptr, img_stream);

	// Tell libpng that we already read MAGIC_NUMBER_SIZE bytes.
	png_set_sig_bytes(png_ptr, MAGIC_NUMBER_SIZE);

	// Read all the info up to the image data.
	png_read_info(png_ptr, info_ptr);

	// get info about png
	int bit_depth, color_type;
	png_uint_32 width, height;
	png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, 0, 0, 0);
	image->width = width;
	image->height = height;

	if (bit_depth != 8) return -1; // TODO unsupported

	GLint format;
	switch (color_type)
	{
	case PNG_COLOR_TYPE_RGB:
		format = GL_RGB;
		break;

	case PNG_COLOR_TYPE_RGB_ALPHA:
		format = GL_RGBA;
		break;

	default:
		fclose(img_stream);
		png_destroy_read_struct(&png_ptr, &info_ptr, &end_ptr);
		close(img);
		return -1;
	}

	// Row size in bytes.
	// glTexImage2d requires rows to be 4-byte aligned
	unsigned rowbytes = png_get_rowbytes(png_ptr, info_ptr);
	rowbytes += 3 - ((rowbytes - 1) % 4);

	png_byte **rows = malloc(image->height * (sizeof(png_byte *) + rowbytes * sizeof(png_byte)) + 15); // TODO why + 15
	if (!rows)
	{
		fclose(img_stream);
		png_destroy_read_struct(&png_ptr, &info_ptr, &end_ptr);
		close(img);
		return -1;
	}

	// set the individual row_pointers to point at the correct offsets
	size_t i;
	png_byte *image_data = (png_byte *)(rows + image->height);
	for(i = 0; i < image->height; i++)
		rows[image->height - 1 - i] = image_data + i * rowbytes;

	// read the png into image_data through row_pointers
	png_read_image(png_ptr, rows);

	fclose(img_stream);
	png_destroy_read_struct(&png_ptr, &info_ptr, &end_ptr);
	close(img);

	if (modify)
		modify(image, rows);

	// Generate the OpenGL texture object.
	glGenTextures(1, &image->texture);
	glBindTexture(GL_TEXTURE_2D, image->texture);
	glTexImage2D(GL_TEXTURE_2D, 0, format, image->width, image->height, 0, format, GL_UNSIGNED_BYTE, image_data);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	free(rows);

	return 0;
}

void image_grayscale(const struct image *restrict image, png_byte **rows)
{
	for(size_t y = 0; y < image->height; ++y)
	{
		for(size_t x = 0; x < image->width; ++x)
		{
			unsigned p0 = rows[y][x * 4];
			unsigned p1 = rows[y][x * 4 + 1];
			unsigned p2 = rows[y][x * 4 + 2];

			unsigned n = sqrt((p0 * p0 + p1 * p1 + p2 * p2) / 3);
			rows[y][x * 4] = n;
			rows[y][x * 4 + 1] = n;
			rows[y][x * 4 + 2] = n;
		}
	}
}

void image_mask(const struct image *restrict image, png_byte **rows)
{
	for(size_t y = 0; y < image->height; ++y)
	{
		for(size_t x = 0; x < image->width; ++x)
		{
			if ((rows[y][x * 4] != 255) || (rows[y][x * 4 + 1] != 255) || (rows[y][x * 4 + 2] != 255))
			{
				rows[y][x * 4 + 3] = 0;
			}
		}
	}
}

void image_draw(const struct image *restrict image, unsigned x, unsigned y)
{
//	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4ub(255, 255, 255, 255); // TODO why is this necessary
	glBindTexture(GL_TEXTURE_2D, image->texture);

	glEnable(GL_TEXTURE_2D);
	glBegin(GL_QUADS);

	glTexCoord2d(1, 0);
	glVertex2f(x + image->width, y + image->height);

	glTexCoord2d(0, 0);
	glVertex2f(x, y + image->height);

	glTexCoord2d(0, 1);
	glVertex2f(x, y);

	glTexCoord2d(1, 1);
	glVertex2f(x + image->width, y);

	glEnd();
	glDisable(GL_TEXTURE_2D);
}

void image_draw_mask(const struct image *restrict image, unsigned x, unsigned y, const unsigned char color[static 4])
{
//	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glColor4ub(color[0], color[1], color[2], color[3]);
	glBindTexture(GL_TEXTURE_2D, image->texture);

	glEnable(GL_TEXTURE_2D);
	glBegin(GL_QUADS);

	glTexCoord2d(1, 0);
	glVertex2f(x + image->width, y + image->height);

	glTexCoord2d(0, 0);
	glVertex2f(x, y + image->height);

	glTexCoord2d(0, 1);
	glVertex2f(x, y);

	glTexCoord2d(1, 1);
	glVertex2f(x + image->width, y);

	glEnd();
	glDisable(GL_TEXTURE_2D);
}

void display_image(const struct image *restrict image, unsigned x, unsigned y, unsigned width, unsigned height)
{
//	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4ub(255, 255, 255, 255); // TODO why is this necessary
	glBindTexture(GL_TEXTURE_2D, image->texture);

	glEnable(GL_TEXTURE_2D);
	glBegin(GL_QUADS);

	glTexCoord2d(width / image->width, 0);
	glVertex2f(x + width, y + height);

	glTexCoord2d(0, 0);
	glVertex2f(x, y + height);

	glTexCoord2d(0, height / image->height);
	glVertex2f(x, y);

	glTexCoord2d(width / image->width, height / image->height);
	glVertex2f(x + width, y);

	glEnd();
	glDisable(GL_TEXTURE_2D);
}

void image_unload(struct image *restrict image)
{
	glDeleteTextures(1, &image->texture);
}
