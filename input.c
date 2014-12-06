#include <stdlib.h>

#define GL_GLEXT_PROTOTYPES

#include <GL/glx.h>
#include <GL/glext.h>

#include <xcb/xcb.h>

#include "types.h"
#include "json.h"
#include "map.h"
#include "battle.h"
#include "input.h"
#include "interface.h"
#include "region.h"

#define INPUT_NOTME 1
#define INPUT_DONE 2
#define INPUT_TERMINATE 3

#define EVENT_MOTION -127

extern xcb_connection_t *connection;
extern KeySym *keymap;
extern int keysyms_per_keycode;
extern int keycode_min, keycode_max;
extern GLuint map_framebuffer;

struct area
{
	unsigned left, right, top, bottom;
	int (*callback)(int, unsigned, unsigned, uint16_t, const struct game *restrict);
};

extern struct pawn *(*battlefield)[BATTLEFIELD_WIDTH];

static struct state state;

static int input_local(void (*display)(const struct player *restrict, const struct state *restrict, const struct game *restrict), const struct area *restrict areas, size_t areas_count, const struct game *restrict game)
{
	xcb_generic_event_t *event;
	xcb_button_release_event_t *mouse;
	xcb_key_press_event_t *keyboard;
	xcb_motion_notify_event_t *motion;

	int code; // TODO this is oversimplification
	unsigned x, y;
	uint16_t modifiers;

	size_t index;
	int status;

	display(game->players, &state, game);

	// TODO clear queued events (previously pressed keys, etc.)

	while (1)
	{
		// TODO consider using xcb_poll_for_event()
		event = xcb_wait_for_event(connection);
		if (!event) return -1;

		switch (event->response_type & ~0x80)
		{
		case XCB_EXPOSE:
			display(game->players, &state, game);
			continue;

		case XCB_BUTTON_PRESS:
			mouse = (xcb_button_release_event_t *)event;
			code = -mouse->detail;
			x = mouse->event_x;
			y = mouse->event_y;
			modifiers = mouse->state;
			break;

		case XCB_KEY_PRESS:
			keyboard = (xcb_key_press_event_t *)event;
			code = keymap[(keyboard->detail - keycode_min) * keysyms_per_keycode];
			x = keyboard->event_x;
			y = keyboard->event_y;
			modifiers = keyboard->state;
			break;

			//KeySym *input = keymap + (keyboard->detail - keycode_min) * keysyms_per_keycode;
			//printf("%d %c %c %c %c\n", (int)*input, (int)input[0], (int)input[1], (int)input[2], (int)input[3]);

		case XCB_MOTION_NOTIFY:
			motion = (xcb_motion_notify_event_t *)event;
			code = EVENT_MOTION;
			x = motion->event_x;
			y = motion->event_y;
			modifiers = motion->state;
			break;

		default:
			continue;
		}

		free(event);

		// Propagate the event until someone handles it.
		index = areas_count - 1;
		do
		{
			if ((areas[index].left <= x) && (x <= areas[index].right) && (areas[index].top <= y) && (y <= areas[index].bottom))
			{
				status = areas[index].callback(code, x - areas[index].left, y - areas[index].top, modifiers, game);
				if (status == INPUT_TERMINATE) return -1;
				else if (status == INPUT_DONE) return 0;
				else if (status == INPUT_NOTME) continue;
				else break;
			}
		} while (index--);
		display(game->players, &state, game);
	}
}

static int input_turn(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	switch (code)
	{
	case 'q':
		return INPUT_TERMINATE;

	case 'n':
		return INPUT_DONE;

	case EVENT_MOTION:
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
		if (!pixel_color[0]) state.region = -1;
		else state.region = pixel_color[2];

		state.selected.slot = 0;
	}
	else if (code == -3)
	{
		struct region *region = game->regions + state.region;
		struct slot *slot;

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

static int input_slot(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	struct slot *slot;
	size_t offset;
	int found;

	if (code >= 0) return INPUT_NOTME; // ignore keyboard events

	if (state.region < 0) goto reset; // no region selected
	slot = game->regions[state.region].slots;
	if (!slot) goto reset; // no slots in this region

	// Find which slot was clicked.
	if ((x % (FIELD_SIZE + 3)) >= FIELD_SIZE) goto reset; // no slot clicked
	offset = x / (FIELD_SIZE + 3);
	if (offset >= buildings_count) goto reset; // no slot clicked

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
			.left = SLOT_X(0),
			.right = SLOT_X(0) + 7 * (FIELD_SIZE + MARGIN) - MARGIN - 1,
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

	return input_local(if_map, areas, sizeof(areas) / sizeof(*areas), game);
}

// Sets move destination of a pawn. Returns -1 if the current player is not allowed to move the pawn at the destination.
static int pawn_move(const struct player *restrict players, struct pawn *restrict pawn, unsigned x, unsigned y)
{
	// TODO support fast move
	if ((state.player == pawn->slot->player) && reachable(players, battlefield, pawn, x, y))
	{
		// Reset shoot commands.
		pawn->shoot.x = -1;
		pawn->shoot.y = -1;

		pawn->move.x[1] = x;
		pawn->move.y[1] = y;

		return 0;
	}
	else return -1;
}

// Sets shoot target of a pawn. Returns -1 if the current player is not allowed to shoot at the target with this pawn.
static int pawn_shoot(const struct player *restrict players, struct pawn *restrict pawn, unsigned x, unsigned y)
{
	if ((state.player == pawn->slot->player) && shootable(players, battlefield, pawn, x, y))
	{
		// Reset move commands.
		pawn->move.x[1] = pawn->move.x[0];
		pawn->move.y[1] = pawn->move.y[0];

		pawn->shoot.x = x;
		pawn->shoot.y = y;

		return 0;
	}
	else return -1;
}

static int input_round(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	return ((code == 'n') ? INPUT_DONE : 0);
}

static int input_pawn(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME;

	if ((state.x == BATTLEFIELD_WIDTH) || (state.y == BATTLEFIELD_HEIGHT)) return 0;
	struct pawn *pawn = battlefield[state.y][state.x];
	if (!pawn) return 0; // no field selected

	if (code == -1)
	{
		if ((x % (FIELD_SIZE + PAWN_MARGIN)) >= FIELD_SIZE) goto reset;

		// Select the clicked pawn.
		int offset = x / (FIELD_SIZE + PAWN_MARGIN);
		int found;
		while (1)
		{
			found = (pawn->slot->player == state.player);
			if (!found || offset)
			{
				pawn = pawn->_next;
				if (!pawn) goto reset;
				if (found) offset -= 1;
			}
			else if (found) break;
		}
		state.selected.pawn = pawn;
	}

	return 0;

reset:
	// Make sure no pawn is selected.
	state.selected.pawn = 0;
	return 0;
}

static int input_field(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME;

	x /= FIELD_SIZE;
	y /= FIELD_SIZE;

	// if (modifiers & XCB_MOD_MASK_SHIFT) ; // TODO fast move

	if (code == -1)
	{
		// Set current field.
		state.x = x;
		state.y = y;
		state.selected.pawn = 0;
	}
	else if (code == -3)
	{
		// shoot if CONTROL is pressed; move otherwise
		// If there is a pawn selected, apply the command just to it.
		// Otherwise apply the command to each pawn on the current field.
		if (state.selected.pawn)
		{
			if (modifiers & XCB_MOD_MASK_CONTROL) pawn_shoot(game->players, state.selected.pawn, x, y);
			else pawn_move(game->players, state.selected.pawn, x, y);
		}
		else
		{
			struct pawn *pawn;
			for(pawn = battlefield[state.y][state.x]; pawn; pawn = pawn->_next)
			{
				if (modifiers & XCB_MOD_MASK_CONTROL) pawn_shoot(game->players, pawn, x, y);
				else pawn_move(game->players, pawn, x, y);
			}
		}
	}

	return 0;
}

int input_battle(const struct game *restrict game, unsigned char player)
{
	struct area areas[] = {
		{
			.left = 0,
			.right = SCREEN_WIDTH - 1,
			.top = 0,
			.bottom = SCREEN_HEIGHT - 1,
			.callback = input_round
		},
		{
			.left = 0,
			.right = BATTLEFIELD_WIDTH * FIELD_SIZE - 1,
			.top = 0,
			.bottom = BATTLEFIELD_HEIGHT * FIELD_SIZE - 1,
			.callback = input_field
		},
		{
			.left = CTRL_X + 4,
			.right = CTRL_X + 4 + 7 * (FIELD_SIZE + 4) - 4 - 1, // TODO change this 7
			.top = CTRL_Y + 2,
			.bottom = CTRL_Y + 2 + FIELD_SIZE - 1,
			.callback = input_pawn
		},
	};

	state.player = player;

	// Set current field to a field outside of the board.
	state.x = BATTLEFIELD_WIDTH;
	state.y = BATTLEFIELD_HEIGHT;

	state.selected.pawn = 0;

	return input_local(if_battle, areas, sizeof(areas) / sizeof(*areas), game);
}
