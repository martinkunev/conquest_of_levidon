#include <stdlib.h>

#include "types.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "input.h"
#include "input_map.h"
#include "interface.h"

static int input_turn(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_map *state = argument;

	switch (code)
	{
	case 'q':
		return INPUT_TERMINATE;

	case 'n':
		return INPUT_DONE;

	case EVENT_MOTION:
		state->hover_object = HOVER_NONE;
	default:
		return 0;
	}
}

static int input_region(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_map *state = argument;

	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME;

	// TODO write this function better

	// Get the clicked region.
	int region_index = if_storage_get(x, y);

	if (code == -1)
	{
		struct troop *slot;

		if (region_index < 0) state->region = REGION_NONE;
		else state->region = region_index;

		state->troop = 0;

		state->self_offset = 0;
		state->other_offset = 0;

		state->self_count = 0;
		state->other_count = 0;
		for(slot = game->regions[state->region].troops; slot; slot = slot->_next)
			if (slot->player == state->player) state->self_count++;
			else state->other_count++;
	}
	else if (code == -3)
	{
		struct region *region = game->regions + state->region;
		struct troop *slot;

		if (region_index < 0) return 0;

		unsigned index;
		struct region *destination = game->regions + region_index;
		if (destination == region) goto valid;
		for(index = 0; index < 8; ++index)
			if (destination == region->neighbors[index])
				goto valid;
		return 0;

valid:
		if (state->troop)
		{
			// Set the move destination of the selected troop.
			slot = state->troop;
			if (state->player == slot->player)
				slot->move = game->regions + region_index;
		}
		else
		{
			// Set the move destination of all slots in the region.
			for(slot = region->troops; slot; slot = slot->_next)
				if (state->player == slot->player)
					slot->move = game->regions + region_index;
		}
	}

	return 0;
}

static int input_construct(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_map *state = argument;

	ssize_t index;
	struct region *region;

	if (code >= 0) return INPUT_NOTME; // ignore keyboard events

	if (state->region == REGION_NONE) goto reset; // no region selected
	region = game->regions + state->region;
	if (state->player != region->owner) goto reset; // player does not control the region
	if (region->owner != region->garrison.owner) goto reset; // the garrison is under siege

	// Find which building was clicked.
	index = if_index(Building, (struct point){x, y});
	if ((index < 0) || (index >= buildings_count)) goto reset; // no building clicked
	if (!region_building_available(region, buildings[index])) goto reset; // building can not be constructed

	if (code == -1)
	{
		if (region->construct >= 0) // there is a construction in process
		{
			// If the building clicked is the one under construction, cancel the construction.
			// If the construction has not yet started, return allocated resources.
			if (region->construct == index)
			{
				// build_cancel(game, region, index);
				if (region->build_progress)
					region->build_progress = 0;
				else
					resource_add(&game->players[state->player].treasury, &buildings[index].cost);
				region->construct = -1;
			}
		}
		else if (!region_built(region, index))
		{
			// if (build_start(game, region, index)) ...;
			if (!resource_enough(&game->players[state->player].treasury, &buildings[index].cost)) return 0;

			region->construct = index;
			resource_subtract(&game->players[state->player].treasury, &buildings[index].cost);
		}
	}
	else if (code == EVENT_MOTION)
	{
		state->hover_object = HOVER_BUILDING;
		state->hover.building = index;
	}

	return 0;

reset:
	state->hover_object = HOVER_NONE;
	return 0;
}

static int input_train(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_map *state = argument;

	ssize_t unit;
	struct region *region;

	if (code >= 0) return INPUT_NOTME; // ignore keyboard events

	if (state->region == REGION_NONE) goto reset; // no region selected
	region = game->regions + state->region;
	if (state->player != region->owner) goto reset; // player does not control the region
	if (region->owner != region->garrison.owner) goto reset; // the garrison is under siege

	// Find which unit was clicked.
	unit = if_index(Inventory, (struct point){x, y});
	if ((unit < 0) || (unit >= UNITS_COUNT)) goto reset; // no unit clicked
	if (!region_unit_available(region, UNITS[unit])) goto reset; // unit can not be trained

	if (code == -1)
	{
		size_t index;

		if (!resource_enough(&game->players[state->player].treasury, &UNITS[unit].cost)) return 0;

		for(index = 0; index < TRAIN_QUEUE; ++index)
			if (!region->train[index])
			{
				// Spend the money required for the units.
				resource_subtract(&game->players[state->player].treasury, &UNITS[unit].cost);

				region->train[index] = UNITS + unit;
				break;
			}
	}
	else if (code == EVENT_MOTION)
	{
		state->hover_object = HOVER_UNIT;
		state->hover.unit = unit;
	}

	return 0;

reset:
	state->hover_object = HOVER_NONE;
	return 0;
}

static int input_dismiss(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_map *state = argument;

	ssize_t index;
	struct region *region;

	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME;

	if (state->region == REGION_NONE) return 0;
	region = game->regions + state->region;
	if (state->player != region->owner) return 0; // player does not control the region
	if (region->owner != region->garrison.owner) return 0; // the garrison is under siege

	// Find which train order was clicked.
	index = if_index(Dismiss, (struct point){x, y});
	if ((index < 0) || (index >= TRAIN_QUEUE)) return 0; // no train order clicked

	if (code == -1)
	{
		//const struct unit **train = region->train;

		if (!region->train[index]) return 0; // no train order clicked

		// If the training has not yet started, return allocated resources.
		// Else, reset training information.
		if (index || !game->regions[state->region].train_time)
			resource_add(&game->players[state->player].treasury, &region->train[index]->cost);
		else
			game->regions[state->region].train_time = 0;

		for(index += 1; index < TRAIN_QUEUE; ++index)
			region->train[index - 1] = region->train[index];
		region->train[TRAIN_QUEUE - 1] = 0;
	}

	return 0;
}

static int input_scroll_self(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_map *state = argument;

	if (code != -1) return INPUT_NOTME; // handle only left mouse clicks

	if (state->region == REGION_NONE) return 0; // no region selected
	struct troop *slot = game->regions[state->region].troops;
	if (!slot) return 0; // no slots in this region

	if (x <= object_group[TroopSelf].left - 1) // scroll left
	{
		if (state->self_offset)
		{
			state->self_offset -= TROOPS_VISIBLE;
			state->troop = 0;
		}
	}
	else if (x >= object_group[TroopSelf].right + 1) // scroll right
	{
		if ((state->self_offset + TROOPS_VISIBLE) < state->self_count)
		{
			state->self_offset += TROOPS_VISIBLE;
			state->troop = 0;
		}
	}
}

static int input_scroll_ally(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_map *state = argument;

	if (code != -1) return INPUT_NOTME; // handle only left mouse clicks

	if (state->region == REGION_NONE) return 0; // no region selected
	struct troop *slot = game->regions[state->region].troops;
	if (!slot) return 0; // no slots in this region

	if (x <= object_group[TroopOther].left - 1) // scroll left
	{
		if (state->other_offset)
		{
			state->other_offset -= TROOPS_VISIBLE;
			state->troop = 0;
		}
	}
	else if (x >= object_group[TroopOther].right + 1) // scroll right
	{
		if ((state->other_offset + TROOPS_VISIBLE) < state->other_count)
		{
			state->other_offset += TROOPS_VISIBLE;
			state->troop = 0;
		}
	}
}

static int input_troop(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct region *region;
	struct troop *troop;
	ssize_t offset;
	int found;

	struct state_map *state = argument;

	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME; // ignore keyboard events

	if (state->region == REGION_NONE) goto reset; // no region selected
	region = game->regions + state->region;
	troop = region->troops;
	if (!troop) goto reset; // no troops in this region

	// Find which troop was clicked.
	offset = if_index(Building, (struct point){x, y});
	if (offset < 0) goto reset; // no troop clicked
	offset += state->self_offset;

	// Find the clicked troop in the linked list.
	while (1)
	{
		found = (troop->player == state->player);
		if (!found || offset)
		{
			troop = troop->_next;
			if (!troop) goto reset; // no troop clicked
			if (found) offset -= 1;
		}
		else if (found) break;
	}

	if (code == -1) state->troop = troop;
	else if (code == -3)
	{
		if (!state->troop) return 0; // no troop selected
		if (troop == state->troop) return 0; // the clicked and the selected troop are the same
		if (troop->unit != state->troop->unit) return 0; // the clicked and the selected units are different

		// Transfer units from the selected troop to the clicked troop.
		unsigned transfer_count = (SLOT_UNITS - troop->count);
		if (state->troop->count > transfer_count)
		{
			state->troop->count -= transfer_count;
			troop->count += transfer_count;
		}
		else
		{
			troop->count += state->troop->count;

			// Remove the selected troop because all units were transfered to the clicked troop.
			troop = state->troop;
			troop_detach(&region->troops, troop);
			free(troop);

			goto reset;
		}
	}

	return 0;

reset:
	// Make sure no slot is selected.
	state->troop = 0;
	return 0;
}

static int input_garrison(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct region *region;
	struct troop *troop;
	size_t garrison;
	ssize_t index;

	struct state_map *state = argument;

	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME; // ignore keyboard events

	if (state->region == REGION_NONE) return 0;
	region = game->regions + state->region;
	if (state->player != region->garrison.owner) return 0; // current player does not control the garrison

	if (region_built(region, BuildingFortress)) garrison = FORTRESS;
	else if (region_built(region, BuildingPalisade)) garrison = PALISADE;
	else return 0; // no garrison in this region

	if (code == -1)
	{
		troop = region->garrison.troops;
		if (!troop) return 0; // no troops in the garrison

		// Find which troop was clicked.
		index = if_index(Building, (struct point){x, y});
		if (index < 0) return 0; // no troop clicked

		// Find the clicked troop in the linked list.
		while (index)
		{
			troop = troop->_next;
			if (!troop) return 0; // no troop clicked
			index -= 1;
		}

		// Move the clicked troop out of the garrison.
		troop_detach(&region->garrison.troops, troop);
		troop_attach(&region->troops, troop);
	}
	else if (code == -3)
	{
		if (!state->troop) return 0; // no troop selected

		// Count how many units are in the garrison.
		unsigned count = 0;
		for(troop = region->garrison.troops; troop; troop = troop->_next)
			count += 1;

		if (count < garrison_info[garrison].troops) // if there is place for one more troop
		{
			// Move the selected troop to the garrison.
			troop_detach(&region->troops, state->troop);
			troop_attach(&region->garrison.troops, state->troop);
		}

		state->troop = 0;
	}

	return 0;
}

int input_map(const struct game *restrict game, unsigned char player)
{
	struct area areas[] = {
		{
			.left = 0,
			.right = SCREEN_WIDTH - 1,
			.top = 0,
			.bottom = SCREEN_HEIGHT - 1,
			.callback = input_turn,
		},
		{
			.left = MAP_X,
			.right = MAP_X + MAP_WIDTH - 1,
			.top = MAP_Y,
			.bottom = MAP_Y + MAP_HEIGHT - 1,
			.callback = input_region,
		},
		{
			.left = object_group[Inventory].left,
			.right = object_group[Inventory].right,
			.top = object_group[Inventory].top,
			.bottom = object_group[Inventory].bottom,
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

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_map, game, &state);
}
