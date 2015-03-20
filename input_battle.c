//#include <stdlib.h>

//#include <GL/glx.h>
//#include <GL/glext.h>

#include <xcb/xcb.h>

#include "types.h"
#include "json.h"
#include "map.h"
#include "battlefield.h"
//#include "pathfinding.h"
#include "input.h"
#include "interface.h"

extern struct battlefield (*battlefield)[BATTLEFIELD_WIDTH];

// Sets move destination of a pawn. Returns -1 if the current player is not allowed to move the pawn at the destination.
static void pawn_move(struct pawn *restrict pawn, unsigned x, unsigned y)
{
	struct point target = {x, y};

	// TODO support fast move

	if (!battlefield_reachable(battlefield, pawn, target)) return;

	// TODO set pawn->fight

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
	moves_free(pawn->moves.first->next);
	pawn->moves.first->next = 0;
}

static int input_round(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game)
{
	return ((code == 'n') ? INPUT_DONE : 0);
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
		state.selected.pawn = battlefield[y][x].pawn;
	}
	else if (code == -3)
	{
		struct pawn *pawn = state.selected.pawn;
		if (!pawn) return 0;
		if (pawn->slot->player != state.player) return 0;

		// shoot if CONTROL is pressed; move otherwise
		if (modifiers & XCB_MOD_MASK_CONTROL) pawn_shoot(pawn, x, y);
		else pawn_move(pawn, x, y);
	}

	return 0;
}

int input_battle(const struct game *restrict game, const struct battle *restrict battle, unsigned char player)
{
	if_set(battle->field);

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

	state.player = player;

	// Set current field to a field outside of the board.
	state.x = BATTLEFIELD_WIDTH;
	state.y = BATTLEFIELD_HEIGHT;

	state.selected.pawn = 0;

	return input_local(if_battle, areas, sizeof(areas) / sizeof(*areas), game);
}
