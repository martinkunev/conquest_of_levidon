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

extern struct battle *battle;

static int input_round(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	// TODO battle must be passed as argument

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

static int attack(const struct game *restrict game, const struct battle *restrict battle, struct state_battle *restrict state, struct point target)
{
	int x = target.x, y = target.y;

	// If the pawn is not next to its target, move before attacking it.
	if (!battlefield_neighbors(state->field, target))
	{
		double move_distance = INFINITY;
		int move_x, move_y;

		if ((x > 0) && !battle->field[y][x - 1].pawn && (state->reachable[y][x - 1] < move_distance))
		{
			move_x = x - 1;
			move_y = y;
			move_distance = state->reachable[move_y][move_x];
		}
		if ((x < (BATTLEFIELD_WIDTH - 1)) && !battle->field[y][x + 1].pawn && (state->reachable[y][x + 1] < move_distance))
		{
			move_x = x + 1;
			move_y = y;
			move_distance = state->reachable[move_y][move_x];
		}
		if ((y > 0) && !battle->field[y - 1][x].pawn && (state->reachable[y - 1][x] < move_distance))
		{
			move_x = x;
			move_y = y - 1;
			move_distance = state->reachable[move_y][move_x];
		}
		if ((y < (BATTLEFIELD_HEIGHT - 1)) && !battle->field[y + 1][x].pawn && (state->reachable[y + 1][x] < move_distance))
		{
			move_x = x;
			move_y = y + 1;
			move_distance = state->reachable[move_y][move_x];
		}

		if (move_distance < INFINITY)
		{
			movement_set(state->pawn, (struct point){move_x, move_y}, state->graph, state->obstacles);
			if (path_reachable(state->pawn, state->graph, state->obstacles, state->reachable) < 0)
				return ERROR_MEMORY;
		}
		else return ERROR_MISSING;
	}

	combat_fight(game, battle, state->obstacles, state->pawn, battle->field[y][x].pawn);

	return 0;
}

static int input_field(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	// TODO battle must be passed as argument

	struct state_battle *state = argument;

	int status;

	if (code >= 0) return INPUT_NOTME;

	x /= FIELD_SIZE;
	y /= FIELD_SIZE;

	if (code == -1)
	{
		// Set current field.
		state->field = (struct point){x, y};
		state->pawn = battle->field[y][x].pawn;

		if (state->pawn)
		{
			// Remove fight target if the target is dead.
			// TODO this should not be done here
			if ((state->pawn->action == PAWN_FIGHT) && state->pawn->target.pawn && !state->pawn->target.pawn->troop->count)
				state->pawn->action = 0;

			status = path_reachable(state->pawn, state->graph, state->obstacles, state->reachable);
			if (status < 0) return status;
		}
	}
	else if (code == -3)
	{
		struct pawn *pawn = state->pawn;
		if (!pawn || (pawn->troop->owner != state->player)) return 0;
		struct point target = {x, y};

		// Cancel actions if the clicked field is the one on which the pawn stands.
		if (point_eq(target, pawn->moves[0].location))
		{
			movement_stay(pawn);
			status = path_reachable(pawn, state->graph, state->obstacles, state->reachable);
			if (status < 0) return status;
			pawn->action = 0;
			return 0;
		}

		// TODO memory error checks below?

		// if CONTROL is pressed, shoot
		// if SHIFT is pressed, move
		if (modifiers & XCB_MOD_MASK_CONTROL) combat_shoot(game, battle, state->obstacles, pawn, target);
		else if (modifiers & XCB_MOD_MASK_SHIFT)
		{
			movement_queue(pawn, target, state->graph, state->obstacles);
			status = path_reachable(pawn, state->graph, state->obstacles, state->reachable);
			if (status < 0) return status;
		}
		else
		{
			const struct battlefield *restrict field = &battle->field[y][x];

			// Perform the first reasonable action: shoot, fight, move
			if (field->pawn)
			{
				if (allies(game, pawn->troop->owner, field->pawn->troop->owner))
				{
					movement_set(pawn, target, state->graph, state->obstacles);
					status = path_reachable(pawn, state->graph, state->obstacles, state->reachable);
					if (status < 0) return status;
				}
				else
				{
					if (combat_shoot(game, battle, state->obstacles, pawn, target))
						;
					else
						attack(game, battle, state, target); // TODO error check
				}
			}
			else if (combat_assault(game, battle, state->obstacles, pawn, target))
				;
			else
			{
				movement_set(pawn, target, state->graph, state->obstacles);
				status = path_reachable(pawn, state->graph, state->obstacles, state->reachable);
				if (status < 0) return status;
			}
		}
	}
	else if (code == EVENT_MOTION) state->hover = (struct point){x, y};

	return 0;
}

static int input_place(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	// TODO battle must be passed as argument

	struct state_formation *state = argument;

	if (code >= 0) return INPUT_NOTME;

	x /= FIELD_SIZE;
	y /= FIELD_SIZE;

	// TODO make sure there is no obstacle on the field

	if (code == -1)
	{
		struct pawn *selected = state->pawn;
		struct pawn *new = battle->field[y][x].pawn;

		// If there is a selected pawn, put it at the clicked field.
		// If there is a pawn at the clicked field, take it from there.

		if (selected)
		{
			size_t i = 0;
			struct point location = {x, y};
			for(i = 0; i < state->reachable_count; ++i)
				if ((state->reachable[i].x == location.x) && (state->reachable[i].y == location.y))
					goto reachable;
			return 0;

reachable:
			selected->moves[0].location = location;
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
	}
	else if (code == EVENT_MOTION) state->hover = (struct point){x, y};

	return 0;
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

int input_battle(const struct game *restrict game, struct battle *restrict battle, unsigned char player)
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

	state.obstacles = path_obstacles(game, battle, player);
	if (!state.obstacles) abort();
	state.graph = visibility_graph_build(battle, state.obstacles);
	if (!state.graph) abort();

	int status = input_local(areas, sizeof(areas) / sizeof(*areas), if_battle, game, &state);

	visibility_graph_free(state.graph);
	free(state.obstacles);

	return status;
}
