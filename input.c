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

#define INPUT_NOTME 1
#define INPUT_DONE 2
#define INPUT_TERMINATE 3

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

static int input_local(void (*display)(const struct player *restrict, const struct state *restrict), const struct area *restrict areas, size_t areas_count, const struct game *restrict game)
{
	xcb_generic_event_t *event;
	xcb_button_release_event_t *mouse;
	xcb_key_press_event_t *keyboard;

	int code; // TODO this is oversimplification
	unsigned x, y;
	uint16_t modifiers;

	size_t index;
	int status;

	display(game->players, &state);

	while (1)
	{
		// TODO consider using xcb_poll_for_event()
		event = xcb_wait_for_event(connection);
		if (!event) return -1;

		switch (event->response_type & ~0x80)
		{
		case XCB_EXPOSE:
			display(game->players, &state);
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
		display(game->players, &state);
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

	default:
		return 0;
	}
}

static int input_region(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
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
	if (code >= 0) return INPUT_NOTME;

	if (state.player != game->regions[state.region].owner) return 0;
	if ((x % (FIELD_SIZE + PAWN_MARGIN)) >= FIELD_SIZE) return 0;

	if (code == -1)
	{
		size_t unit = x / (FIELD_SIZE + PAWN_MARGIN);
		if (unit >= game->units_count) return 0;

		struct unit **train = game->regions[state.region].train;
		size_t index;
		for(index = 0; index < TRAIN_QUEUE; ++index)
			if (!train[index])
			{
				train[index] = (struct unit *)(game->units + unit); // TODO fix this cast
				break;
			}
	}

	return 0;
}

static int input_dismiss(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	if (code >= 0) return INPUT_NOTME;

	if (state.player != game->regions[state.region].owner) return 0;
	if ((x % (FIELD_SIZE + PAWN_MARGIN)) >= FIELD_SIZE) return 0;

	if (code == -1)
	{
		struct unit **train = game->regions[state.region].train;

		size_t index;
		for(index = (x / (FIELD_SIZE + PAWN_MARGIN) + 1); index < TRAIN_QUEUE; ++index)
			train[index - 1] = train[index];
		train[TRAIN_QUEUE - 1] = 0;
	}

	return 0;
}

static int input_slot(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	if (code >= 0) return INPUT_NOTME;

	if (state.region < 0) return 0;
	struct slot *slot = game->regions[state.region].slots;
	if (!slot) return 0; // no field selected

	if (code == -1)
	{
		if ((x % (FIELD_SIZE + PAWN_MARGIN)) >= FIELD_SIZE) goto reset;

		// Select the clicked pawn.
		int offset = x / (FIELD_SIZE + PAWN_MARGIN);
		int found;
		while (1)
		{
			found = (slot->player == state.player);
			if (!found || offset)
			{
				slot = slot->_next;
				if (!slot) goto reset;
				if (found) offset -= 1;
			}
			else if (found) break;
		}
		state.selected.slot = slot;
	}

	return 0;

reset:
	// Make sure no slot is selected.
	state.selected.slot = 0;
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
			.left = PANEL_X,
			.right = PANEL_X + (FIELD_SIZE + 8) * game->units_count - 1,
			.top = PANEL_Y + 260,
			.bottom = PANEL_Y + 260 + FIELD_SIZE - 1,
			.callback = input_train
		},
		{
			.left = PANEL_X,
			.right = PANEL_X + TRAIN_QUEUE * (FIELD_SIZE + PAWN_MARGIN) - 1,
			.top = PANEL_Y + 196,
			.bottom = PANEL_Y + 196 + FIELD_SIZE - 1,
			.callback = input_dismiss
		},
		{
			.left = PANEL_X + 4,
			.right = PANEL_X + 4 + 7 * (FIELD_SIZE + PAWN_MARGIN) - PAWN_MARGIN - 1,
			.top = PANEL_Y,
			.bottom = PANEL_Y + FIELD_SIZE - 1,
			.callback = input_slot,
		}
	};

	state.player = player;

	state.region = -1;
	state.selected.slot = 0;

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
