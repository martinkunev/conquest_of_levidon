#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <xcb/xcb.h>

#include "types.h"
#include "map.h"
#include "pathfinding.h"
#include "movement.h"
#include "battle.h"
#include "combat.h"
#include "input.h"
#include "input_battle.h"
#include "interface.h"

extern const struct battle *battle;
extern struct battlefield (*battlefield)[BATTLEFIELD_WIDTH];

// Sets move destination of a pawn. Returns -1 if the current player is not allowed to move the pawn at the destination.
static void pawn_move(struct pawn *restrict pawn, unsigned x, unsigned y, struct state_battle *state, int queue)
{
	struct point target = {x, y};

	// TODO ? set pawn->fight

	if (queue)
	{
		if (movement_queue(pawn, target, state->nodes)) return;
	}
	else
	{
		if (movement_set(pawn, target, state->nodes)) return;
	}

	// Reset shoot commands.
	pawn->shoot = POINT_NONE;
}

// Sets shoot target of a pawn. Returns -1 if the current player is not allowed to shoot at the target with this pawn.
static void pawn_shoot(struct pawn *restrict pawn, unsigned x, unsigned y)
{
	struct point target = {x, y};

	if (!battlefield_shootable(pawn, target)) return;

	pawn->shoot = target;

	// Reset move commands.
	movement_stay(pawn);
}

static int input_round(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_battle *state = argument; // TODO sometimes this is state_formation

	switch (code)
	{
	case EVENT_MOTION:
		state->hover = POINT_NONE;
		return 0;

	case 'q': // surrender
		{
			// Kill all the pawns of the player.
			size_t i;
			const struct vector *pawns = battle->player_pawns + state->player;
			for(i = 0; i < pawns->length; ++i)
				((struct pawn *)pawns->data[i])->slot->count = 0;
		}
	case 'n':
		return INPUT_DONE;

	default:
		return 0;
	}
}

static int input_field(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_battle *state = argument;

	if (code >= 0) return INPUT_NOTME;

	x /= FIELD_SIZE;
	y /= FIELD_SIZE;

	// TODO ? fast move

	if (code == -1)
	{
		// Set current field.
		state->x = x;
		state->y = y;
		state->pawn = battlefield[y][x].pawn;
	}
	else if (code == -3)
	{
		struct pawn *pawn = state->pawn;
		if (!pawn) return 0;
		if (pawn->slot->player != state->player) return 0;

		struct point target = {x, y};
		if (point_eq(target, pawn->moves[0].location))
		{
			movement_stay(pawn);
			pawn->shoot = POINT_NONE;
			return 0;
		}

		// shoot if CONTROL is pressed; move otherwise
		if (modifiers & XCB_MOD_MASK_CONTROL) pawn_shoot(pawn, x, y);
		else if (modifiers & XCB_MOD_MASK_SHIFT) pawn_move(pawn, x, y, state, 1);
		else pawn_move(pawn, x, y, state, 0);
	}
	else if (code == EVENT_MOTION) state->hover = (struct point){x, y};

	return 0;
}

static int input_place(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_formation *state = argument;

	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME;

	x /= FIELD_SIZE;
	y /= FIELD_SIZE;

	if (code == -1)
	{
		struct pawn *selected = state->pawn;
		struct pawn *new = battlefield[y][x].pawn;

		// If there is a selected pawn, put it at the clicked field.
		// If there is a pawn at the clicked field, take it from there.

		if (selected)
		{
			selected->moves[0].location = (struct point){x, y};
			selected->moves[0].time = 0.0;
		}

		battlefield[y][x].pawn = selected;
		state->pawn = new;
	}
	else if (code == EVENT_MOTION) state->hover = (struct point){x, y};

	return 0;
}

static int input_pawn(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_battle *state = argument;

	if (code == EVENT_MOTION) return INPUT_NOTME;
	if (code >= 0) return INPUT_NOTME;

	if (code == -1)
	{
		if ((x % (FIELD_SIZE + 1)) >= FIELD_SIZE) goto reset;
		if ((y % (FIELD_SIZE + MARGIN)) >= FIELD_SIZE) goto reset;

		// Select the clicked pawn.
		unsigned column = x / (FIELD_SIZE + 1);
		unsigned line = y / (FIELD_SIZE + MARGIN);
		size_t i;
		for(i = 0; i < battle->player_pawns[state->player].length; ++i)
		{
			struct pawn *pawn = battle->player_pawns[state->player].data[i];
			if (line)
			{
				if (pawn->slot->location != game->regions[state->region].neighbors[line - 1]) continue;
			}
			else
			{
				if (pawn->slot->location != game->regions + state->region) continue;
			}

			if (column) column -= 1;
			else
			{
				state->pawn = pawn;
				return 0;
			}
		}
	}

reset:
	// Make sure no pawn is selected.
	state->pawn = 0;
	return 0;
}

int input_formation(const struct game *restrict game, const struct region *restrict region, struct battle *restrict battle, unsigned char player)
{
	if_set(battle->field, battle);

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
			.callback = input_place
		},
	};

	struct state_formation state;

	state.player = player;

	state.pawn = 0;
	state.hover = POINT_NONE;

	state.region = region - game->regions;

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_formation, game, &state);
}

int input_battle(const struct game *restrict game, struct battle *restrict battle, unsigned char player)
{
	if_set(battle->field, battle);

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
	};

	struct state_battle state;

	state.player = player;

	// Set current field to a field outside of the board.
	state.x = BATTLEFIELD_WIDTH;
	state.y = BATTLEFIELD_HEIGHT;

	state.pawn = 0;
	state.hover = POINT_NONE;

	state.nodes = visibility_graph_build(0, 0);
	if (!state.nodes) abort();
	int status = input_local(areas, sizeof(areas) / sizeof(*areas), if_battle, game, &state);
	visibility_graph_free(state.nodes);

	return status;
}
