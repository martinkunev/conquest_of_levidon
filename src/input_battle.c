#include <X11/keysym.h>

#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "movement.h"
#include "combat.h"
#include "input.h"
#include "input_battle.h"
#include "display.h"

extern struct battle *battle;

extern unsigned SCREEN_WIDTH, SCREEN_HEIGHT;

static int input_round(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	// TODO battle must be passed as argument

	struct state_battle *state = argument; // TODO sometimes this is state_formation

	switch (code)
	{
	case EVENT_MOTION:
		if (!point_eq(state->hover, POINT_NONE))
		{
			state->hover = POINT_NONE;
			return 0;
		}
	default:
		return INPUT_IGNORE;

	case XK_Escape:
		{
			int status;
			struct pawn *pawn = state->pawn;
			if (!pawn || (pawn->troop->owner != state->player)) return INPUT_IGNORE;

			// Cancel the actions of the current pawn.

			if ((pawn->moves_count == 1) && !pawn->action)
				return INPUT_IGNORE;

			movement_stay(pawn);
			status = path_reachable(pawn, state->graph, state->obstacles, state->reachable);
			if (status < 0) return status;
			pawn->action = 0;
		}
		return 0;

	case 'q': // surrender
		battle->players[state->player].alive = 0;
	case 'n':
		return INPUT_DONE;
	}
}

static int pawn_command(const struct game *restrict game, const struct battle *restrict battle, struct pawn *restrict pawn, struct point point, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	const struct battlefield *restrict target = &battle->field[point.y][point.x];

	// TODO tower support

	if (target->pawn)
	{
		if (allies(game, pawn->troop->owner, target->pawn->troop->owner))
		{
			return movement_queue(pawn, point, graph, obstacles);
		}
		else if (combat_order_shoot(game, battle, obstacles, pawn, point))
		{
			return 0;
		}
		else
		{
			// If the pawn is not next to its target, move before attacking it.
			int status = movement_attack(pawn, point, battle->field, reachable, graph, obstacles);
			if (status < 0) return status;

			combat_order_fight(game, battle, obstacles, pawn, target->pawn); // the check above ensure this will succeed
			return 0;
		}
	}
	else if ((target->blockage == BLOCKAGE_OBSTACLE) && !allies(game, target->owner, pawn->troop->owner))
	{
		// If the pawn is not next to its target, move before attacking it.
		int status = movement_attack(pawn, point, battle->field, reachable, graph, obstacles);
		if (status < 0) return status;

		combat_order_assault(game, battle, obstacles, pawn, point); // the check above ensure this will succeed
		return 0;
	}
	else
	{
		return movement_queue(pawn, point, graph, obstacles);
	}
}

static int input_field(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	// TODO battle must be passed as argument

	struct state_battle *state = argument;

	int status;

	if (code >= 0) return INPUT_NOTME;

	x /= FIELD_SIZE;
	y /= FIELD_SIZE;

	struct point point = {x, y};

	if (code == EVENT_MOUSE_LEFT)
	{
		if (point_eq(point, state->field))
			return INPUT_IGNORE;

		if (!battle->field[y][x].pawn && !battle->field[y][x].blockage)
		{
			state->field = POINT_NONE;
			state->pawn = 0;
			return 0;
		}

		// Set current field.
		state->field = point;
		state->pawn = battle->field[y][x].pawn;

		if (state->pawn)
		{
			status = path_reachable(state->pawn, state->graph, state->obstacles, state->reachable);
			if (status < 0) return status;
		}

		return 0;
	}
	else if (code == EVENT_MOUSE_RIGHT)
	{
		struct pawn *pawn = state->pawn;
		if (!pawn || (pawn->troop->owner != state->player)) return INPUT_IGNORE;

		// Cancel actions if the clicked field is the one on which the pawn stands.
		if (point_eq(point, pawn->moves[0].location))
		{
			if ((pawn->moves_count == 1) && !pawn->action)
				return INPUT_IGNORE;

			movement_stay(pawn);
			pawn->action = 0;
			status = path_reachable(pawn, state->graph, state->obstacles, state->reachable);
			if (status < 0) return status;
			return 0;
		}

		// if CONTROL is pressed, shoot
		// if SHIFT is pressed, don't overwrite the current command
		if (modifiers & XCB_MOD_MASK_CONTROL) combat_order_shoot(game, battle, state->obstacles, pawn, point);
		else if (modifiers & XCB_MOD_MASK_SHIFT)
		{
			status = pawn_command(game, battle, pawn, point, state->graph, state->obstacles, state->reachable);
			switch (status)
			{
			case 0:
				status = path_reachable(pawn, state->graph, state->obstacles, state->reachable);
				if (status < 0) return status;
			case ERROR_MISSING:
				return 0;

			case ERROR_MEMORY:
				return status;
			}
		}
		else
		{
			double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH];

			// Erase moves but remember their count so that we can restore them if necessary.
			size_t moves_count = pawn->moves_count;
			pawn->moves_count = 1;

			// Initialize reachable distances for stationary pawn.
			status = path_reachable(pawn, state->graph, state->obstacles, reachable);
			if (status < 0) return status;

			status = pawn_command(game, battle, pawn, point, state->graph, state->obstacles, reachable);
			switch (status)
			{
			case 0:
				memcpy(state->reachable, reachable, sizeof(state->reachable));
				return 0;

			case ERROR_MISSING:
				// The command failed. Restore moves.
				pawn->moves_count = moves_count;
				return 0;

			case ERROR_MEMORY:
				return status;
			}
		}
	}
	else if (code == EVENT_MOTION)
	{
		if (!point_eq(state->hover, point))
		{
			state->hover = point;
			return 0;
		}
	}

	return INPUT_IGNORE;
}

static int input_place(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	// TODO battle must be passed as argument

	struct state_formation *state = argument;

	if (code >= 0) return INPUT_NOTME;

	x /= FIELD_SIZE;
	y /= FIELD_SIZE;

	struct point point = {x, y};

	if (code == -1)
	{
		struct pawn *selected = state->pawn;
		struct pawn *new = battle->field[y][x].pawn;

		// If there is a selected pawn, put it at the clicked field.
		// If there is a pawn at the clicked field, take it from there.

		if (selected)
		{
			size_t i = 0;
			for(i = 0; i < state->reachable_count; ++i)
				if (point_eq(state->reachable[i], point))
					goto reachable;
			return INPUT_IGNORE;

reachable:
			selected->moves[0].location = point;
			selected->moves[0].time = 0.0;
		}

		battle->field[y][x].pawn = selected;
		state->pawn = new;

		if (new)
		{
			if (battle->assault)
				state->reachable_count = formation_reachable_assault(game, battle, new, state->reachable);
			else
				state->reachable_count = formation_reachable_open(game, battle, new, state->reachable);
		}

		return 0;
	}
	else if (code == EVENT_MOTION)
	{
		if (!point_eq(state->hover, point))
		{
			state->hover = point;
			return 0;
		}
	}

	return INPUT_IGNORE;
}

int input_formation(const struct game *restrict game, struct battle *restrict battle, unsigned char player)
{
	if_set(battle); // TODO remove this

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

int input_battle(const struct game *restrict game, struct battle *restrict battle, unsigned char player, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles)
{
	if_set(battle); // TODO remove this

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

	// No selected field or pawn.
	state.field = POINT_NONE;
	state.pawn = 0;
	state.hover = POINT_NONE;

	state.obstacles = obstacles;
	state.graph = graph;

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_battle, game, &state);
}
