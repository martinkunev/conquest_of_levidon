#include <stdlib.h>

#define GL_GLEXT_PROTOTYPES

#include <GL/glx.h>

#include "types.h"
#include "json.h"
#include "map.h"
#include "pathfinding.h"
#include "battlefield.h"
#include "input.h"
#include "input_map.h"
#include "interface.h"
#include "region.h"

extern GLuint map_framebuffer;

struct state state;
//struct state_map state;

static int input_turn(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	switch (code)
	{
	case 'q':
		return INPUT_TERMINATE;

	case 'n':
		return INPUT_DONE;

	case EVENT_MOTION:
		//state.hover_type = HOVER_NONE;
		state.pointed.building = -1;
		state.pointed.unit = -1;
	default:
		return 0;
	}
}

static int input_region(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME;

	// TODO write this function better

	// Get the clicked region.
	GLubyte pixel_color[3];
	glBindFramebuffer(GL_FRAMEBUFFER, map_framebuffer);
	glReadPixels(x, MAP_HEIGHT - y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel_color);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (code == -1)
	{
		struct troop *slot;

		if (!pixel_color[0]) state.region = -1;
		else state.region = pixel_color[2];

		state.selected.slot = 0;

		state.self_offset = 0;
		state.ally_offset = 0;

		state.self_count = 0;
		state.ally_count = 0;
		for(slot = game->regions[state.region].slots; slot; slot = slot->_next)
			if (slot->player == state.player) state.self_count++;
			else state.ally_count++;
	}
	else if (code == -3)
	{
		struct region *region = game->regions + state.region;
		struct troop *slot;

		if (!pixel_color[0]) return 0;

		unsigned index;
		struct region *destination = game->regions + pixel_color[2];
		if (destination == region) goto valid;
		for(index = 0; index < 8; ++index)
			if (destination == region->neighbors[index])
				goto valid;
		return 0;

valid:
		if (state.selected.slot)
		{
			// Set the move destination of the selected slot.
			slot = state.selected.slot;
			if (state.player == slot->player)
				slot->move = game->regions + pixel_color[2];
		}
		else
		{
			// Set the move destination of all slots in the region.
			for(slot = region->slots; slot; slot = slot->_next)
				if (state.player == slot->player)
					slot->move = game->regions + pixel_color[2];
		}
	}

	return 0;
}

static int input_train(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	size_t unit;
	struct region *region;

	if (code >= 0) return INPUT_NOTME; // ignore keyboard events

	if (state.region < 0) goto reset; // no region selected
	region = game->regions + state.region;
	if (state.player != region->owner) goto reset; // player does not control the region

	// Find which unit was clicked.
	if ((x % (FIELD_SIZE + 1)) >= FIELD_SIZE) goto reset; // no unit clicked
	unit = x / (FIELD_SIZE + 1);
	if (unit >= game->units_count) goto reset; // no unit clicked

	if (!region_order_available(region, game->units[unit].requires)) goto reset;

	if (code == -1)
	{
		size_t index;

		if (!resource_enough(&game->players[state.player].treasury, &game->units[unit].cost)) return 0;

		for(index = 0; index < TRAIN_QUEUE; ++index)
			if (!region->train[index])
			{
				// Spend the money required for the units.
				resource_subtract(&game->players[state.player].treasury, &game->units[unit].cost);

				region->train[index] = (struct unit *)(game->units + unit); // TODO fix this cast
				break;
			}
	}
	else if (code == EVENT_MOTION)
	{
		state.pointed.building = -1;
		state.pointed.unit = unit;
	}

	return 0;

reset:
	state.pointed.unit = -1;
	return 0;
}

static int input_dismiss(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME;

	if (state.region < 0) return 0;
	if (state.player != game->regions[state.region].owner) return 0;
	if ((x % (FIELD_SIZE + 1)) >= FIELD_SIZE) return 0;

	if (code == -1)
	{
		struct unit **train = game->regions[state.region].train;
		size_t index = (x / (FIELD_SIZE + 1));

		if (!train[index]) return 0;

		// If the unit is not yet trained, return the spent resources.
		// Else, reset training information.
		if (index || !game->regions[state.region].train_time)
			resource_add(&game->players[state.player].treasury, &train[index]->cost);
		else
			game->regions[state.region].train_time = 0;

		for(index += 1; index < TRAIN_QUEUE; ++index)
			train[index - 1] = train[index];
		train[TRAIN_QUEUE - 1] = 0;
	}

	return 0;
}

static int input_scroll_self(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	if (code != -1) return INPUT_NOTME; // handle only left mouse clicks

	if (state.region < 0) return 0; // no region selected
	struct troop *slot = game->regions[state.region].slots;
	if (!slot) return 0; // no slots in this region

	if (x < SCROLL) // scroll left
	{
		if (state.self_offset)
		{
			state.self_offset -= SLOTS_VISIBLE;
			state.selected.slot = 0;
		}
	}
	else if (x >= SCROLL + 1 + SLOTS_VISIBLE * (FIELD_SIZE + 1)) // scroll right
	{
		if ((state.self_offset + SLOTS_VISIBLE) < state.self_count)
		{
			state.self_offset += SLOTS_VISIBLE;
			state.selected.slot = 0;
		}
	}
}

static int input_scroll_ally(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	if (code != -1) return INPUT_NOTME; // handle only left mouse clicks

	if (state.region < 0) return 0; // no region selected
	struct troop *slot = game->regions[state.region].slots;
	if (!slot) return 0; // no slots in this region

	if (x < SCROLL) // scroll left
	{
		if (state.ally_offset)
		{
			state.ally_offset -= SLOTS_VISIBLE;
			state.selected.slot = 0;
		}
	}
	else if (x >= SCROLL + 1 + SLOTS_VISIBLE * (FIELD_SIZE + 1)) // scroll right
	{
		if ((state.ally_offset + SLOTS_VISIBLE) < state.ally_count)
		{
			state.ally_offset += SLOTS_VISIBLE;
			state.selected.slot = 0;
		}
	}
}

static int input_slot(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	struct troop *slot;
	size_t offset;
	int found;

	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME; // ignore keyboard events

	if (state.region < 0) goto reset; // no region selected
	slot = game->regions[state.region].slots;
	if (!slot) goto reset; // no slots in this region

	// Find which slot was clicked.
	if ((x % (FIELD_SIZE + 1)) >= FIELD_SIZE) goto reset; // no slot clicked
	offset = state.self_offset + x / (FIELD_SIZE + 1);

	// Find the clicked slot.
	while (1)
	{
		found = (slot->player == state.player);
		if (!found || offset)
		{
			slot = slot->_next;
			if (!slot) goto reset; // no slot clicked
			if (found) offset -= 1;
		}
		else if (found) break;
	}

	if (code == -1) state.selected.slot = slot;
	else if (code == -3)
	{
		if (!state.selected.slot) return 0; // no slot selected
		if (slot == state.selected.slot) return 0; // the clicked and the selected slot are the same
		if (slot->unit != state.selected.slot->unit) return 0; // the clicked and the selected units are different

		// Transfer units from the selected slot to the clicked slot.
		unsigned transfer_count = (SLOT_UNITS - slot->count);
		if (state.selected.slot->count > transfer_count)
		{
			state.selected.slot->count -= transfer_count;
			slot->count += transfer_count;
		}
		else
		{
			slot->count += state.selected.slot->count;

			// Remove the selected slot because all units were transfered to the clicked slot.
			slot = state.selected.slot;
			if (slot->_prev) slot->_prev->_next = slot->_next;
			else game->regions[state.region].slots = slot->_next;
			if (slot->_next) slot->_next->_prev = slot->_prev;
			free(slot);

			goto reset;
		}
	}

	return 0;

reset:
	// Make sure no slot is selected.
	state.selected.slot = 0;
	return 0;
}

static int input_build(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	size_t index;
	struct region *region;

	if (code >= 0) return INPUT_NOTME; // ignore keyboard events

	if (state.region < 0) goto reset; // no region selected
	region = game->regions + state.region;
	if (state.player != region->owner) goto reset; // player does not control the region

	// Find which building was clicked.
	if ((x % (FIELD_SIZE + 1)) >= FIELD_SIZE) goto reset; // no building clicked
	index = x / (FIELD_SIZE + 1);
	if (index >= buildings_count) goto reset; // no building clicked

	if (!region_order_available(region, buildings[index].requires)) goto reset;

	if (code == -1)
	{
		signed char *construct = &region->construct;

		if (*construct >= 0) // there is a construction in process
		{
			// If the building clicked is the one under construction, cancel the construction.
			if (*construct == index)
			{
				if (region->construct_time)
					region->construct_time = 0;
				else
					resource_add(&game->players[state.player].treasury, &buildings[index].cost);
				*construct = -1;
			}
		}
		else if (!(region->built & (1 << index)))
		{
			if (!resource_enough(&game->players[state.player].treasury, &buildings[index].cost)) return 0;
			*construct = index;
			resource_subtract(&game->players[state.player].treasury, &buildings[index].cost);
		}
	}
	else if (code == EVENT_MOTION)
	{
		state.pointed.building = index;
		state.pointed.unit = -1;
	}

	return 0;

reset:
	state.pointed.building = -1;
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
			.callback = input_turn
		},
		{
			.left = MAP_X,
			.right = MAP_X + MAP_WIDTH - 1,
			.top = MAP_Y,
			.bottom = MAP_Y + MAP_HEIGHT - 1,
			.callback = input_region
		},
		{
			.left = INVENTORY_X(0),
			.right = INVENTORY_X(0) + game->units_count * (FIELD_SIZE + 1) - 1 - 1,
			.top = INVENTORY_Y,
			.bottom = INVENTORY_Y + FIELD_SIZE - 1,
			.callback = input_train
		},
		{
			.left = TRAIN_X(0),
			.right = TRAIN_X(0) + TRAIN_QUEUE * (FIELD_SIZE + 1) - 1 - 1,
			.top = TRAIN_Y,
			.bottom = TRAIN_Y + FIELD_SIZE - 1,
			.callback = input_dismiss
		},
		{
			.left = SLOT_X(0) - 1 - SCROLL,
			.right = SLOT_X(SLOTS_VISIBLE) + SCROLL - 1,
			.top = SLOT_Y(0),
			.bottom = SLOT_Y(0) + FIELD_SIZE - 1,
			.callback = input_scroll_self,
		},
		{
			.left = SLOT_X(0) - 1 - SCROLL,
			.right = SLOT_X(SLOTS_VISIBLE) + SCROLL - 1,
			.top = SLOT_Y(1),
			.bottom = SLOT_Y(1) + FIELD_SIZE - 1,
			.callback = input_scroll_ally,
		},
		{
			.left = SLOT_X(0),
			.right = SLOT_X(SLOTS_VISIBLE) - 1 - 1,
			.top = SLOT_Y(0),
			.bottom = SLOT_Y(0) + FIELD_SIZE - 1,
			.callback = input_slot,
		},
		{
			.left = BUILDING_X(0),
			.right = BUILDING_X(0) + buildings_count * (FIELD_SIZE + 1) - 1 - 1,
			.top = BUILDING_Y,
			.bottom = BUILDING_Y + FIELD_SIZE - 1,
			.callback = input_build,
		},
	};

	state.player = player;

	state.region = -1;
	state.selected.slot = 0;

	state.pointed.building = -1;
	state.pointed.unit = -1;

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_map, game, &state);
}
