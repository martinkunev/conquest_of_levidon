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

struct image
{
	GLuint texture;
	uint32_t width, height;
};

#include <png.h>
int image_load_png(struct image *restrict image, const char *restrict filename, void (*modify)(const struct image *restrict, png_byte **));

void image_grayscale(const struct image *restrict image, png_byte **rows);
void image_mask(const struct image *restrict image, png_byte **rows);

void image_draw(const struct image *restrict image, unsigned x, unsigned y);
void image_draw_mask(const struct image *restrict image, unsigned x, unsigned y, const unsigned char color[static 4]);
void display_image(const struct image *restrict image, unsigned x, unsigned y, unsigned width, unsigned height);

void image_unload(struct image *restrict image);

extern struct image image_flag, image_flag_small;
extern struct image image_selected, image_panel, image_construction, image_movement, image_assault, image_dismiss;
extern struct image image_pawn_guard, image_pawn_fight, image_pawn_assault, image_pawn_shoot;
extern struct image image_shoot_right, image_shoot_up, image_shoot_left, image_shoot_down;
extern struct image image_terrain[1];
extern struct image image_garrisons[2];
extern struct image image_map_village, image_map_garrison[2];
extern struct image image_gold, image_food, image_wood, image_stone, image_iron, image_time;
extern struct image image_scroll_left, image_scroll_right;
extern struct image image_units[7], image_units_mask[7];
extern struct image image_buildings[13], image_buildings_gray[13];
extern struct image image_palisade[16], image_palisade_gate[2], image_fortress[16], image_fortress_gate[2];
extern struct image image_economy;
