#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <xcb/xcb.h>

#include "types.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "movement.h"
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
		if (movement_queue(pawn, target, state->graph, state->obstacles)) return;
	}
	else
	{
		if (movement_set(pawn, target, state->graph, state->obstacles)) return;
	}

	// Reset shoot commands.
	pawn->shoot = POINT_NONE;
}

// Sets shoot target of a pawn. Returns -1 if the current player is not allowed to shoot at the target with this pawn.
static void pawn_shoot(struct pawn *restrict pawn, unsigned x, unsigned y, struct state_battle *state)
{
	struct point target = {x, y};

	if (!battlefield_shootable(pawn, target, state->obstacles)) return;

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
			struct pawn *const *pawns = battle->players[state->player].pawns;
			size_t pawns_count = battle->players[state->player].pawns_count;
			for(i = 0; i < pawns_count; ++i)
				pawns[i]->troop->count = 0;
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
		if (pawn->troop->owner != state->player) return 0;

		struct point target = {x, y};
		if (point_eq(target, pawn->moves[0].location))
		{
			movement_stay(pawn);
			pawn->shoot = POINT_NONE;
			return 0;
		}

		// shoot if CONTROL is pressed; move otherwise
		if (modifiers & XCB_MOD_MASK_CONTROL) pawn_shoot(pawn, x, y, state);
		else if (modifiers & XCB_MOD_MASK_SHIFT) pawn_move(pawn, x, y, state, 1);
		else pawn_move(pawn, x, y, state, 0);
	}
	else if (code == EVENT_MOTION) state->hover = (struct point){x, y};

	return 0;
}

static int input_place(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_formation *state = argument;

	if (code >= 0) return INPUT_NOTME;

	x /= FIELD_SIZE;
	y /= FIELD_SIZE;

	// TODO make sure there is no obstacle on the field

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

int input_formation(const struct game *restrict game, struct battle *restrict battle, unsigned char player)
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
			.left = object_group[Battlefield].left,
			.right = object_group[Battlefield].right,
			.top = object_group[Battlefield].top,
			.bottom = object_group[Battlefield].bottom,
			.callback = input_place
		},
	};

	struct state_formation state;

	state.player = player;

	state.pawn = 0;
	state.hover = POINT_NONE;

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_formation, game, &state);
}

int input_battle(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct obstacle *restrict obstacles, size_t obstacles_count)
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
			.left = object_group[Battlefield].left,
			.right = object_group[Battlefield].right,
			.top = object_group[Battlefield].top,
			.bottom = object_group[Battlefield].bottom,
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

	state.obstacles = path_obstacles(game, battle, player);
	if (!state.obstacles) abort();
	state.graph = visibility_graph_build(battle, state.obstacles);
	if (!state.graph) abort();

	int status = input_local(areas, sizeof(areas) / sizeof(*areas), if_battle, game, &state);

	visibility_graph_free(state.graph);
	free(state.obstacles);

	return status;
}
