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

#include <stdint.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>

#include "format.h"
#include "game.h"
#include "draw.h"
#include "map.h"
#include "pathfinding.h"
#include "image.h"
#include "interface.h"
#include "display_common.h"

#define PREFIX_IMG PREFIX "share/conquest_of_levidon/img/"

#define S(s) (s), sizeof(s) - 1

struct image image_flag, image_flag_small;
struct image image_selected, image_panel, image_construction, image_movement, image_assault;
struct image image_pawn_fight, image_pawn_assault, image_pawn_shoot;
struct image image_shoot_right, image_shoot_up, image_shoot_left, image_shoot_down; // TODO more directions?
struct image image_terrain[1];
struct image image_garrisons[2]; // TODO this must be big enough for all garrison types
struct image image_map_village, image_map_garrison[2]; // TODO this must be big enough for all garrison types
struct image image_gold, image_food, image_wood, image_stone, image_iron, image_time;
struct image image_scroll_left, image_scroll_right;
struct image image_units[7]; // TODO the array must be enough to hold units_count units
struct image image_buildings[13]; // TODO the array must be big enough to hold buildings_count elements
struct image image_buildings_gray[13]; // TODO the array must be big enough to hold buildings_count elements
struct image image_palisade[16], image_palisade_gate[2], image_fortress[16], image_fortress_gate[2];

void if_load_images(void)
{
	image_load_png(&image_selected, PREFIX_IMG "selected.png", 0);
	image_load_png(&image_flag, PREFIX_IMG "flag.png", 0);
	image_load_png(&image_flag_small, PREFIX_IMG "flag_small.png", 0);
	image_load_png(&image_panel, PREFIX_IMG "panel.png", 0);
	image_load_png(&image_construction, PREFIX_IMG "construction.png", 0);
	image_load_png(&image_movement, PREFIX_IMG "movement.png", 0);
	image_load_png(&image_assault, PREFIX_IMG "assault.png", 0);

	image_load_png(&image_pawn_fight, PREFIX_IMG "pawn_fight.png", 0);
	image_load_png(&image_pawn_assault, PREFIX_IMG "pawn_assault.png", 0);
	image_load_png(&image_pawn_shoot, PREFIX_IMG "pawn_shoot.png", 0);

	image_load_png(&image_shoot_right, PREFIX_IMG "shoot_right.png", 0);
	image_load_png(&image_shoot_up, PREFIX_IMG "shoot_up.png", 0);
	image_load_png(&image_shoot_left, PREFIX_IMG "shoot_left.png", 0);
	image_load_png(&image_shoot_down, PREFIX_IMG "shoot_down.png", 0);

	image_load_png(&image_garrisons[PALISADE], PREFIX_IMG "garrison_palisade.png", 0);
	image_load_png(&image_garrisons[FORTRESS], PREFIX_IMG "garrison_fortress.png", 0);

	image_load_png(&image_map_village, PREFIX_IMG "map_village.png", 0);
	image_load_png(&image_map_garrison[PALISADE], PREFIX_IMG "map_palisade.png", 0);
	image_load_png(&image_map_garrison[FORTRESS], PREFIX_IMG "map_fortress.png", 0);

	image_load_png(&image_scroll_left, PREFIX_IMG "scroll_left.png", 0);
	image_load_png(&image_scroll_right, PREFIX_IMG "scroll_right.png", 0);

	image_load_png(&image_gold, PREFIX_IMG "gold.png", 0);
	image_load_png(&image_food, PREFIX_IMG "food.png", 0);
	image_load_png(&image_wood, PREFIX_IMG "wood.png", 0);
	image_load_png(&image_stone, PREFIX_IMG "stone.png", 0);
	image_load_png(&image_iron, PREFIX_IMG "iron.png", 0);
	image_load_png(&image_time, PREFIX_IMG "time.png", 0);

	image_load_png(&image_units[UnitPeasant], PREFIX_IMG "peasant.png", 0);
	image_load_png(&image_units[UnitMilitia], PREFIX_IMG "militia.png", 0);
	image_load_png(&image_units[UnitPikeman], PREFIX_IMG "pikeman.png", 0);
	image_load_png(&image_units[UnitArcher], PREFIX_IMG "archer.png", 0);
	image_load_png(&image_units[UnitLongbow], PREFIX_IMG "longbow.png", 0);
	image_load_png(&image_units[UnitLightCavalry], PREFIX_IMG "light_cavalry.png", 0);
	image_load_png(&image_units[UnitBatteringRam], PREFIX_IMG "battering_ram.png", 0);

	image_load_png(&image_buildings[0], PREFIX_IMG "farm.png", 0);
	image_load_png(&image_buildings[1], PREFIX_IMG "irrigation.png", 0);
	image_load_png(&image_buildings[2], PREFIX_IMG "sawmill.png", 0);
	image_load_png(&image_buildings[3], PREFIX_IMG "mine.png", 0);
	image_load_png(&image_buildings[4], PREFIX_IMG "bloomery.png", 0);
	image_load_png(&image_buildings[5], PREFIX_IMG "barracks.png", 0);
	image_load_png(&image_buildings[6], PREFIX_IMG "archery_range.png", 0);
	image_load_png(&image_buildings[7], PREFIX_IMG "stables.png", 0);
	image_load_png(&image_buildings[8], PREFIX_IMG "watch_tower.png", 0);
	image_load_png(&image_buildings[9], PREFIX_IMG "palisade.png", 0);
	image_load_png(&image_buildings[10], PREFIX_IMG "fortress.png", 0);
	image_load_png(&image_buildings[11], PREFIX_IMG "workshop.png", 0);
	image_load_png(&image_buildings[12], PREFIX_IMG "forge.png", 0);

	image_load_png(&image_buildings_gray[0], PREFIX_IMG "farm.png", 1);
	image_load_png(&image_buildings_gray[1], PREFIX_IMG "irrigation.png", 1);
	image_load_png(&image_buildings_gray[2], PREFIX_IMG "sawmill.png", 1);
	image_load_png(&image_buildings_gray[3], PREFIX_IMG "mine.png", 1);
	image_load_png(&image_buildings_gray[4], PREFIX_IMG "bloomery.png", 1);
	image_load_png(&image_buildings_gray[5], PREFIX_IMG "barracks.png", 1);
	image_load_png(&image_buildings_gray[6], PREFIX_IMG "archery_range.png", 1);
	image_load_png(&image_buildings_gray[7], PREFIX_IMG "stables.png", 1);
	image_load_png(&image_buildings_gray[8], PREFIX_IMG "watch_tower.png", 1);
	image_load_png(&image_buildings_gray[9], PREFIX_IMG "palisade.png", 1);
	image_load_png(&image_buildings_gray[10], PREFIX_IMG "fortress.png", 1);
	image_load_png(&image_buildings_gray[11], PREFIX_IMG "workshop.png", 1);
	image_load_png(&image_buildings_gray[12], PREFIX_IMG "forge.png", 1);

	image_load_png(&image_terrain[0], PREFIX_IMG "terrain_grass.png", 0);

	// Load battlefield images.
	size_t i;
	for(i = 1; i < 16; ++i) // TODO fix this
	{
		char buffer[64], *end; // TODO make sure this is enough

		end = format_bytes(buffer, S(PREFIX_IMG "palisade"));
		end = format_uint(end, i, 10);
		end = format_bytes(end, S(".png"));
		*end = 0;
		image_load_png(&image_palisade[i], buffer, 0);

		end = format_bytes(buffer, S(PREFIX_IMG "fortress"));
		end = format_uint(end, i, 10);
		end = format_bytes(end, S(".png"));
		*end = 0;
		image_load_png(&image_fortress[i], buffer, 0);
	}
	image_load_png(&image_palisade_gate[0], PREFIX_IMG "palisade_gate0.png", 0);
	image_load_png(&image_fortress_gate[0], PREFIX_IMG "fortress_gate0.png", 0);
	image_load_png(&image_palisade_gate[1], PREFIX_IMG "palisade_gate1.png", 0);
	image_load_png(&image_fortress_gate[1], PREFIX_IMG "fortress_gate1.png", 0);
}

void display_troop(size_t unit, unsigned x, unsigned y, enum color color, enum color text, unsigned count)
{
	fill_rectangle(x, y, FIELD_SIZE, FIELD_SIZE, display_colors[color]);
	image_draw(&image_units[unit], x, y);

	if (count)
	{
		char buffer[16];
		size_t length = format_uint(buffer, count, 10) - (uint8_t *)buffer;
		draw_string(buffer, length, x + (FIELD_SIZE - (length * 10)) / 2, y + FIELD_SIZE, &font12, text);
	}
}

void show_flag(unsigned x, unsigned y, unsigned player)
{
	fill_rectangle(x + 4, y + 4, 24, 12, display_colors[Player + player]);
	image_draw(&image_flag, x, y);
}

void show_flag_small(unsigned x, unsigned y, unsigned player)
{
	fill_rectangle(x + 2, y + 2, 12, 6, display_colors[Player + player]);
	image_draw(&image_flag_small, x, y);
}

void show_button(const unsigned char *label, size_t label_size, unsigned x, unsigned y)
{
	struct box box = string_box(label, label_size, &font12);
	draw_rectangle(x - 1, y - 1, BUTTON_WIDTH + 2, BUTTON_HEIGHT + 2, display_colors[White]);
	fill_rectangle(x, y, BUTTON_WIDTH, BUTTON_HEIGHT, display_colors[Black]);
	draw_string(label, label_size, x + (BUTTON_WIDTH - box.width) / 2, y + (BUTTON_HEIGHT - box.height) / 2, &font12, White);
}
