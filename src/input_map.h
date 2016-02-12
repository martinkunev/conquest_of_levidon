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

enum {REGION_NONE = -1};

struct state_map
{
	unsigned char player; // current player

	ssize_t region; // index of the selected region
	struct troop *troop; // selected troop

	//enum {HOVER_NONE, HOVER_TROOP, HOVER_UNIT, HOVER_BUILDING, HOVER_DISMISS} hover_object; // type of the hovered object
	enum {HOVER_NONE, HOVER_UNIT, HOVER_BUILDING} hover_object; // type of the hovered object
	union
	{
		int troop;
		int unit;
		int building;
		int dismiss;
	} hover; // index of the hovered object

	unsigned self_offset, other_offset;
	unsigned self_count, other_count;

	unsigned char regions_visible[REGIONS_LIMIT];
};

int input_map(const struct game *restrict game, unsigned char player);
