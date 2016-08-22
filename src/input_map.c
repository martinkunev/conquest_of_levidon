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
#include <stdlib.h>

#include <X11/keysym.h>

#include "errors.h"
#include "game.h"
#include "draw.h"
#include "map.h"
#include "resources.h"
#include "pathfinding.h"
#include "input.h"
#include "input_map.h"
#include "input_menu.h"
#include "display_common.h"
#include "display_map.h"

/*
#include <stdio.h>
#include "computer_map.h"
double rate(const struct game *restrict game, unsigned char player, struct region_info *restrict regions_info, const struct context *restrict context);
static struct context context;
static struct region_info *regions_info;
*/

static int input_turn(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_map *state = argument;

	switch (code)
	{
	case EVENT_MOTION:
		if (state->hover_object != HOVER_NONE)
		{
			state->hover_object = HOVER_NONE;
			return 0;
		}
	default:
		return INPUT_IGNORE;

	case XK_Delete:
		if (state->troop && (state->troop->move != LOCATION_GARRISON))
		{
			state->troop->dismiss = !state->troop->dismiss;
			return 0;
		}
		return INPUT_IGNORE;

	case XK_Escape:
		return input_save(game);

	case 'n':
		return INPUT_FINISH;
	}
}

static int input_menu(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	if (code != EVENT_MOUSE_LEFT) return INPUT_NOTME;
	return input_save(game);
}

static int input_region(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_map *state = argument;

	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME;

	// Get the clicked region.
	int region_index = if_storage_get(x, y);

	if (code == EVENT_MOUSE_LEFT)
	{
		struct troop *troop;

		if (region_index < 0)
		{
			state->region = REGION_NONE;
			return 0;
		}
		else state->region = region_index;

		state->troop = 0;

		state->self_offset = 0;
		state->other_offset = 0;

		state->self_count = 0;
		state->other_count = 0;
		for(troop = game->regions[state->region].troops; troop; troop = troop->_next)
		{
			if (troop->owner == state->player)
			{
				if (troop->move != LOCATION_GARRISON)
					state->self_count++;
			}
			else if (troop->location != LOCATION_GARRISON)
			{
				state->other_count++;
			}
		}

		return 0;
	}
	else if (code == EVENT_MOUSE_RIGHT)
	{
		if (region_index < 0) return INPUT_IGNORE;

		struct region *region = game->regions + state->region;
		struct troop *troop;

		unsigned index;
		struct region *destination = game->regions + region_index;
		if (destination == region) goto valid;

		// A troop can only go to a neighboring region and only if it's not sieged in the garrison.
		if (!allies(game, state->player, region->owner))
			return INPUT_IGNORE;
		for(index = 0; index < NEIGHBORS_LIMIT; ++index)
			if (destination == region->neighbors[index])
				goto valid;
		return INPUT_IGNORE;

valid:
		if (state->troop)
		{
			if (state->troop->dismiss)
				return INPUT_IGNORE;

			// Set the move destination of the selected troop.
			troop = state->troop;
			if (state->player == troop->owner)
				troop->move = destination;
		}
		else
		{
			// Set the move destination of all troops in the region.
			for(troop = region->troops; troop; troop = troop->_next)
			{
				if (troop->owner != state->player) continue;
				if (troop->move == LOCATION_GARRISON) continue;
				if (troop->dismiss) continue;
				troop->move = destination;
			}
		}

//printf("rating=%f\n", rate(game, state->player, regions_info, &context));
		return 0;
	}

	return INPUT_NOTME;
}

static int input_construct(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_map *state = argument;

	ssize_t building;
	struct region *region;

	if (code >= 0) return INPUT_NOTME; // ignore keyboard events

	if (state->region == REGION_NONE) return INPUT_NOTME; // no region selected
	region = game->regions + state->region;
	if (state->player != region->owner) return INPUT_NOTME; // player does not control the region
	if (region->owner != region->garrison.owner) return INPUT_NOTME; // the garrison is under siege

	// Find which building was clicked.
	building = if_index(Building, (struct point){x, y});
	if ((building < 0) || (building >= BUILDINGS_COUNT)) return INPUT_NOTME; // no building clicked
	if (!region_building_available(region, BUILDINGS + building)) return INPUT_NOTME; // building can not be constructed

	if (code == EVENT_MOUSE_LEFT)
	{
		if (region->construct >= 0) // there is a construction in progress
		{
			// If the building clicked is the one under construction, cancel the construction.
			// If the construction has not yet started, return allocated resources.
			if (region->construct == building)
			{
				// build_cancel(game, region, building);
				if (region->build_progress)
					region->build_progress = 0;
				else
					resource_subtract(&game->players[state->player].treasury, &BUILDINGS[building].cost);
				region->construct = -1;
			}
		}
		else if (!region_built(region, building))
		{
			// if (build_start(game, region, building)) ...;
			if (!resource_enough(&game->players[state->player].treasury, &BUILDINGS[building].cost)) return INPUT_IGNORE;

			region->construct = building;
			resource_add(&game->players[state->player].treasury, &BUILDINGS[building].cost);
		}
	}
	else if (code == EVENT_MOTION)
	{
		if ((state->hover_object == HOVER_BUILDING) && (state->hover.building == building))
			return INPUT_IGNORE;
		state->hover_object = HOVER_BUILDING;
		state->hover.building = building;
	}
	else return INPUT_IGNORE;

	return 0;
}

static int input_train(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_map *state = argument;

	ssize_t unit;
	struct region *region;

	if (code >= 0) return INPUT_NOTME; // ignore keyboard events

	if (state->region == REGION_NONE) return INPUT_NOTME; // no region selected
	region = game->regions + state->region;
	if (state->player != region->owner) return INPUT_NOTME; // player does not control the region
	if (region->owner != region->garrison.owner) return INPUT_NOTME; // the garrison is under siege

	// Find which unit was clicked.
	unit = if_index(Train, (struct point){x, y});
	if ((unit < 0) || (unit >= UNITS_COUNT)) return INPUT_NOTME; // no unit clicked
	if (!region_unit_available(region, UNITS[unit])) return INPUT_NOTME; // unit can not be trained

	if (code == EVENT_MOUSE_LEFT)
	{
		if (!resource_enough(&game->players[state->player].treasury, &UNITS[unit].cost)) return INPUT_IGNORE;

		for(size_t index = 0; index < TRAIN_QUEUE; ++index)
			if (!region->train[index])
			{
				// Spend the money required for the units.
				resource_add(&game->players[state->player].treasury, &UNITS[unit].cost);

				region->train[index] = UNITS + unit;
				break;
			}
	}
	else if (code == EVENT_MOTION)
	{
		if ((state->hover_object == HOVER_UNIT) && (state->hover.unit == unit))
			return INPUT_IGNORE;
		state->hover_object = HOVER_UNIT;
		state->hover.unit = unit;
	}
	else return INPUT_IGNORE;

	return 0;
}

static int input_dismiss(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_map *state = argument;

	ssize_t index;
	struct region *region;

	if (code == EVENT_MOTION) return INPUT_IGNORE;
	if (code >= 0) return INPUT_NOTME;

	if (state->region == REGION_NONE) return INPUT_IGNORE;
	region = game->regions + state->region;
	if (state->player != region->owner) return INPUT_IGNORE; // player does not control the region
	if (region->owner != region->garrison.owner) return INPUT_IGNORE; // the garrison is under siege

	// Find which train order was clicked.
	index = if_index(Dismiss, (struct point){x, y});
	if ((index < 0) || (index >= TRAIN_QUEUE)) return INPUT_IGNORE; // no train order clicked

	if (code == EVENT_MOUSE_LEFT)
	{
		//const struct unit **train = region->train;

		if (!region->train[index]) return INPUT_IGNORE; // no train order clicked

		// If the training has not yet started, return allocated resources.
		// Else, reset training information.
		if (index || !game->regions[state->region].train_progress)
			resource_subtract(&game->players[state->player].treasury, &region->train[index]->cost);
		else
			game->regions[state->region].train_progress = 0;

		for(index += 1; index < TRAIN_QUEUE; ++index)
			region->train[index - 1] = region->train[index];
		region->train[TRAIN_QUEUE - 1] = 0;

		return 0;
	}
	else return INPUT_IGNORE;
}

static int input_scroll_self(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_map *state = argument;

	if (code != EVENT_MOUSE_LEFT) return INPUT_NOTME; // handle only left mouse clicks

	if (state->region == REGION_NONE) return INPUT_IGNORE; // no region selected

	if (x <= object_group[TroopSelf].left - 1) // scroll left
	{
		if (state->self_offset)
		{
			state->self_offset -= TROOPS_VISIBLE;
			state->troop = 0;
			return 0;
		}
		else return INPUT_IGNORE;
	}
	else if (x >= object_group[TroopSelf].right + 1) // scroll right
	{
		if ((state->self_offset + TROOPS_VISIBLE) < state->self_count)
		{
			state->self_offset += TROOPS_VISIBLE;
			state->troop = 0;
			return 0;
		}
		else return INPUT_IGNORE;
	}
	else return INPUT_NOTME;
}

static int input_scroll_ally(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_map *state = argument;

	if (code != EVENT_MOUSE_LEFT) return INPUT_NOTME; // handle only left mouse clicks

	if (state->region == REGION_NONE) return INPUT_IGNORE; // no region selected

	if (x <= object_group[TroopOther].left - 1) // scroll left
	{
		if (state->other_offset)
		{
			state->other_offset -= TROOPS_VISIBLE;
			state->troop = 0;
			return 0;
		}
		else return INPUT_IGNORE;
	}
	else if (x >= object_group[TroopOther].right + 1) // scroll right
	{
		if ((state->other_offset + TROOPS_VISIBLE) < state->other_count)
		{
			state->other_offset += TROOPS_VISIBLE;
			state->troop = 0;
			return 0;
		}
		else return INPUT_IGNORE;
	}
	else return INPUT_NOTME;
}

static int input_troop(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct region *region;
	struct troop *troop;
	ssize_t offset;

	struct state_map *state = argument;

	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME; // ignore keyboard events

	if (state->region == REGION_NONE) goto reset; // no region selected
	region = game->regions + state->region;

	// Find which troop was clicked.
	offset = if_index(TroopSelf, (struct point){x, y});
	if (offset < 0) goto reset; // no troop clicked
	offset += state->self_offset;

	// Find the clicked troop in the linked list.
	for(troop = region->troops; troop; troop = troop->_next)
	{
		if (troop->owner != state->player)
			continue; // skip troops owned by other players
		if ((troop->move == LOCATION_GARRISON) && (troop->owner == region->garrison.owner))
			continue; // skip garrison troops

		if (offset) offset -= 1;
		else break;
	}

	if (code == EVENT_MOUSE_LEFT)
	{
		if (!troop) goto reset; // no troop clicked
		state->troop = troop;
	}
	else if (code == EVENT_MOUSE_RIGHT)
	{
		if (!state->troop) return INPUT_IGNORE; // no troop selected

		// If the selected troop is in the garrison, move it out.
		if ((state->troop->move == LOCATION_GARRISON) && (state->troop->owner == region->garrison.owner))
		{
			state->troop->move = region;
			state->troop = 0;
//printf("rating=%f\n", rate(game, state->player, regions_info, &context));
		}
		else return INPUT_IGNORE;
	}
	else return INPUT_IGNORE;

	return 0;

reset:
	// Make sure no troop is selected.
	state->troop = 0;
	return 0;
}

static int input_garrison(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct region *region;
	struct troop *troop;
	const struct garrison_info *restrict garrison;
	ssize_t offset;

	struct state_map *state = argument;

	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME; // ignore keyboard events

	if (state->region == REGION_NONE) return 0;
	region = game->regions + state->region;

	garrison = garrison_info(region);
	if (!garrison) return 0; // no garrison in this region

	if (code == EVENT_MOUSE_LEFT)
	{
		if (state->player != region->garrison.owner) return 0; // current player does not control the garrison

		// Find which troop was clicked.
		offset = if_index(TroopGarrison, (struct point){x, y});
		if (offset < 0) return 0; // no troop clicked

		// Find the clicked troop in the linked list.
		for(troop = region->troops; 1; troop = troop->_next)
		{
			if (!troop) goto reset; // no troop clicked

			if (troop->owner != state->player)
				continue; // skip troops owned by other players
			if ((troop->move != LOCATION_GARRISON) || (troop->owner != region->garrison.owner))
				continue; // skip non-garrison troops

			if (offset) offset -= 1;
			else break;
		}

		state->troop = troop;
	}
	else if (code == EVENT_MOUSE_RIGHT)
	{
		if (!state->troop) return INPUT_IGNORE; // no troop selected
		if (state->troop->move == LOCATION_GARRISON) return INPUT_IGNORE; // troop already in the garrison
		if (state->troop->dismiss) return INPUT_IGNORE;

		if (state->player == region->garrison.owner)
		{
			if (!region_garrison_full(region, garrison))
			{
				// Move the selected troop to the garrison.
				state->troop->move = LOCATION_GARRISON;
				state->troop = 0;
			}
		}
		else if (!allies(game, state->player, region->garrison.owner))
		{
			state->troop->move = LOCATION_GARRISON;
		}
//printf("rating=%f\n", rate(game, state->player, regions_info, &context));
	}
	else return INPUT_IGNORE;

	return 0;

reset:
	// Make sure no troop is selected.
	state->troop = 0;
	return 0;
}

int input_map(const struct game *restrict game, unsigned char player)
{
	extern unsigned WINDOW_WIDTH, WINDOW_HEIGHT;

	struct area areas[] = {
		{
			.left = 0,
			.right = WINDOW_WIDTH - 1,
			.top = 0,
			.bottom = WINDOW_HEIGHT - 1,
			.callback = input_turn,
		},
		{
			.left = BUTTON_READY_X,
			.right = BUTTON_READY_X + BUTTON_WIDTH,
			.top = BUTTON_READY_Y,
			.bottom = BUTTON_READY_Y + BUTTON_HEIGHT,
			.callback = input_finish,
		},
		{
			.left = BUTTON_MENU_X,
			.right = BUTTON_MENU_X + BUTTON_WIDTH,
			.top = BUTTON_MENU_Y,
			.bottom = BUTTON_MENU_Y + BUTTON_HEIGHT,
			.callback = input_menu,
		},
		{
			.left = MAP_X,
			.right = MAP_X + MAP_WIDTH - 1,
			.top = MAP_Y,
			.bottom = MAP_Y + MAP_HEIGHT - 1,
			.callback = input_region,
		},
		{
			.left = object_group[Train].left,
			.right = object_group[Train].right,
			.top = object_group[Train].top,
			.bottom = object_group[Train].bottom,
			.callback = input_train,
		},
		{
			.left = object_group[Dismiss].left,
			.right = object_group[Dismiss].right,
			.top = object_group[Dismiss].top,
			.bottom = object_group[Dismiss].bottom,
			.callback = input_dismiss,
		},
		{
			.left = object_group[TroopSelf].left - 1 - SCROLL,
			.right = object_group[TroopSelf].right + 1 + SCROLL,
			.top = object_group[TroopSelf].top,
			.bottom = object_group[TroopSelf].bottom,
			.callback = input_scroll_self,
		},
		{
			.left = object_group[TroopOther].left - 1 - SCROLL,
			.right = object_group[TroopOther].right + 1 + SCROLL,
			.top = object_group[TroopOther].top,
			.bottom = object_group[TroopOther].bottom,
			.callback = input_scroll_ally,
		},
		{
			.left = object_group[TroopSelf].left,
			.right = object_group[TroopSelf].right,
			.top = object_group[TroopSelf].top,
			.bottom = object_group[TroopSelf].bottom,
			.callback = input_troop,
		},
		{
			.left = object_group[TroopGarrison].left,
			.right = object_group[TroopGarrison].right,
			.top = object_group[TroopGarrison].top,
			.bottom = object_group[TroopGarrison].bottom,
			.callback = input_garrison,
		},
		{
			.left = object_group[Building].left,
			.right = object_group[Building].right,
			.top = object_group[Building].top,
			.bottom = object_group[Building].bottom,
			.callback = input_construct,
		},
	};

	struct state_map state;

	state.player = player;

	state.region = REGION_NONE;
	state.troop = 0;

	state.hover_object = HOVER_NONE;

	map_visible(game, player, state.regions_visible);

	/*
	struct region_info *regions_info_collect(const struct game *restrict game, unsigned char player, struct context *restrict context);
	regions_info = regions_info_collect(game, player, &context);
	printf("rating=%f\n", rate(game, state.player, regions_info, &context));
	*/

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_map, game, &state);
}
