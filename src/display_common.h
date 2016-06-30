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

#define TABS_X 32
#define TABS_Y 32
#define TAB_PADDING 2

#define PLAYERS_X 384
#define PLAYERS_Y 32
#define PLAYERS_INDICATOR_SIZE 32
#define PLAYERS_PADDING 4

#define WORLDS_X 32
#define WORLDS_Y 56

#define MAP_WIDTH 768
#define MAP_HEIGHT 768
#define MAP_X 256
#define MAP_Y 0

#define BATTLE_X 8
#define BATTLE_Y 8

#define CTRL_X 768
#define CTRL_Y 0
#define CTRL_WIDTH 256
#define CTRL_HEIGHT 768
#define CTRL_MARGIN 32

#define PANEL_X 4
#define PANEL_Y 4
#define PANEL_WIDTH 248
#define PANEL_HEIGHT 760

// TODO fix these values
#define TOOLTIP_X 256
#define TOOLTIP_Y 750

#define FIELD_SIZE 30

#define MARGIN 4

#define SCROLL 8

#define TROOPS_VISIBLE 7
#define TROOPS_GARRISON 6

#define GARRISON_X (PANEL_X + 1)
#define GARRISON_Y (PANEL_Y + 160)
#define GARRISON_MARGIN 24

#define MENU_MESSAGE_X 32
#define MENU_MESSAGE_Y 600

#define TITLE_Y 16
#define LABEL_Y 48
#define REPORT_Y 80

#define BUTTON_WIDTH 120
#define BUTTON_HEIGHT 20

#define BUTTON_ENTER_X 900
#define BUTTON_ENTER_Y 696

#define BUTTON_CANCEL_X 900
#define BUTTON_CANCEL_Y 720

#define BUTTON_EXIT_X 900
#define BUTTON_EXIT_Y 744

#define BUTTON_READY_X 6
#define BUTTON_READY_Y 620

#define BUTTON_MENU_X 130
#define BUTTON_MENU_Y 620

#define BATTLEFIELD_WIDTH 25
#define BATTLEFIELD_HEIGHT 25

enum object {Worlds, WorldTabs, Players, Building, Train, Dismiss, TroopSelf, TroopOther, TroopGarrison, Battlefield};

// rows, columns, left, top, width, height, padding
#define OBJECT_GROUP(r, c, l, t, w, h, p) \
	{ \
		.rows = r, \
		.columns = c, \
		.left = l, \
		.right = l + w * c + p * (c - 1), \
		.top = t, \
		.bottom = t + h * r + p * (r - 1), \
		.width = w, \
		.height = h, \
		.width_padded = w + p, \
		.height_padded = h + p, \
		.span_x = w * c + p * (c - 1), \
		.span_y = h * r + p * (r - 1), \
		.padding = p, \
	}
static const struct object_group
{
	unsigned rows, columns;
	unsigned left, top;
	unsigned right, bottom;
	unsigned width, height;
	unsigned width_padded, height_padded;
	unsigned span_x, span_y;
	unsigned padding;
	unsigned count;
} object_group[] = {
	[Worlds] = OBJECT_GROUP(24, 1, WORLDS_X, WORLDS_Y, 240, 20, 0),
	[WorldTabs] = OBJECT_GROUP(1, 3, TABS_X, TABS_Y, 80, 24, 0),
	[Players] = OBJECT_GROUP(PLAYERS_LIMIT, 1, PLAYERS_X, PLAYERS_Y, 160, PLAYERS_INDICATOR_SIZE, PLAYERS_PADDING),
	[Building] = OBJECT_GROUP(3, 5, PANEL_X + 1, PANEL_Y + 400, 48, 48, 1),
	[Train] = OBJECT_GROUP(1, 7, PANEL_X + 1, PANEL_Y + 340, 32, 32, 1),
	[Dismiss] = OBJECT_GROUP(1, TRAIN_QUEUE, PANEL_X + 81, PANEL_Y + 300, 32, 32, 1),
	[TroopSelf] = OBJECT_GROUP(1, TROOPS_VISIBLE, PANEL_X + SCROLL + 1, PANEL_Y + 36 + 2, 32, 32, 1),
	[TroopOther] = OBJECT_GROUP(1, TROOPS_VISIBLE, PANEL_X + SCROLL + 1, PANEL_Y + 36 + 48 + 2 + 2, 32, 32, 1),
	[TroopGarrison] = OBJECT_GROUP(1, TROOPS_GARRISON, GARRISON_X + 9, GARRISON_Y + GARRISON_MARGIN, 32, 32, 1),
	[Battlefield] = OBJECT_GROUP(BATTLEFIELD_HEIGHT, BATTLEFIELD_WIDTH, BATTLE_X, BATTLE_Y, FIELD_SIZE, FIELD_SIZE, 0),
};
#undef OBJECT_GROUP

static inline struct point if_position(enum object object, unsigned index)
{
	struct point result;
	unsigned columns = object_group[object].columns;
	result.x = object_group[object].left + (index % columns) * object_group[object].width_padded;
	result.y = object_group[object].top + (index / columns) * object_group[object].height_padded;
	return result;
}

static inline int if_index(enum object object, struct point position)
{
	unsigned padding = object_group[object].padding;
	unsigned width_padded = object_group[object].width_padded;
	unsigned height_padded = object_group[object].height_padded;
	if (((position.x + padding) % width_padded) < padding) return -1; // horizontal padding
	if (((position.y + padding) % height_padded) < padding) return -1; // vertical padding
	return (object_group[object].columns * (position.y / height_padded) + (position.x / width_padded));
}

void if_load_images(void);

void display_troop(size_t unit, unsigned x, unsigned y, enum color color, enum color text, unsigned count);

void show_flag(unsigned x, unsigned y, unsigned player);
void show_flag_small(unsigned x, unsigned y, unsigned player);

void show_button(const unsigned char *label, size_t label_size, unsigned x, unsigned y);
