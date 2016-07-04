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
#include <string.h>
#include <sys/time.h>

#include <X11/keysym.h>

#include "errors.h"
#include "game.h"
#include "draw.h"
#include "map.h"
#include "pathfinding.h"
#include "movement.h"
#include "battle.h"
#include "combat.h"
#include "input.h"
#include "input_battle.h"
#include "display_common.h"
#include "display_battle.h"

#define ANIMATION_DURATION 3.0 /* 3s */
#define ANIMATION_SHOOT_DURATION 2.0 /* 2s */

extern struct battle *battle;

extern unsigned SCREEN_WIDTH, SCREEN_HEIGHT;

static int input_round(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	// TODO battle must be passed as argument

	struct state_battle *state = argument; // TODO sometimes this is state_formation

	switch (code)
	{
	default:
		return INPUT_IGNORE;

	case XK_Escape:
		{
			struct pawn *pawn = state->pawn;
			if (!pawn || (pawn->troop->owner != state->player)) return INPUT_IGNORE;

			// Cancel the commands given to the current pawn.
			pawn->action = 0;
			pawn_stay(pawn);

			// TODO init distances for reachable locations for the current pawn

			return 0;
		}
		return 0;

	case 'q': // surrender
		battle->players[state->player].alive = 0;
	case 'n':
		return INPUT_FINISH;
	}
}

static struct pawn *pawn_find(struct battlefield *restrict field, struct position position)
{
	struct pawn **pawns = field->pawns;
	for(size_t i = 0; (i < BATTLEFIELD_PAWNS_LIMIT) && pawns[i]; i += 1)
		if (battlefield_distance(pawns[i]->position, position) < PAWN_RADIUS)
		{
			return pawns[i];
			// TODO init distances for reachable locations for the current pawn
		}
	return 0;
}

// On success, returns 0. On error no changes are made and error code (< 0) is returned.
static int pawn_command(const struct game *restrict game, struct battle *restrict battle, struct pawn *restrict pawn, struct position target, struct adjacency_list *restrict graph, const struct obstacles *restrict obstacles, double reachable[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	struct battlefield *target_field = &battle->field[(size_t)target.y][(size_t)target.x];
	struct pawn *target_pawn = pawn_find(target_field, target);

	// TODO tower support
	// TODO make sure the target position is reachable

	// If there is a pawn at the target position, use the pawn as a target.
	if (target_pawn)
	{
		if (allies(game, pawn->troop->owner, target_pawn->troop->owner))
			return movement_queue(pawn, target, graph, obstacles);
		else if (!pawn->path.count && combat_order_shoot(game, battle, obstacles, pawn, target))
			return 0;
		else if (combat_order_fight(game, battle, obstacles, pawn, target_pawn))
			return 0;
		else
			return ERROR_MISSING; // none of the commands can be executed
	}
	else if (combat_order_assault(game, pawn, target_field))
	{
		return 0;
	}
	else
	{
		// TODO this should cancel pawn action?

		return movement_queue(pawn, target, graph, obstacles);
	}
}

static int input_field(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	// TODO battle must be passed as argument

	struct state_battle *state = argument;

	struct position position;

	if (code >= 0) return INPUT_NOTME;

	position = (struct position){(double)x / FIELD_SIZE, (double)y / FIELD_SIZE};
	x = (unsigned)position.x;
	y = (unsigned)position.y;

	if (code == EVENT_MOUSE_LEFT)
	{
		struct battlefield *field = &battle->field[y][x];

		state->field = (field->blockage ? field : 0);
		state->pawn = pawn_find(field, position);
		// TODO init distances for reachable locations for the current pawn // if (state->pawn) ...

		return 0;
	}
	else if (code == EVENT_MOUSE_RIGHT)
	{
		struct pawn *pawn = state->pawn;
		if (!pawn || (pawn->troop->owner != state->player)) return INPUT_IGNORE;

		// if CONTROL is pressed, shoot
		// if SHIFT is pressed, don't overwrite the current command
		if (modifiers & XCB_MOD_MASK_CONTROL)
		{
			if (combat_order_shoot(game, battle, state->obstacles, pawn, position))
				return 0;
			return INPUT_IGNORE;
		}
		else
		{
			int status;

			// TODO don't change pawn path and action if pawn_command fails
			if (!(modifiers & XCB_MOD_MASK_SHIFT))
				pawn_stay(pawn);
			pawn->action = 0;

			switch (status = pawn_command(game, battle, pawn, position, state->graph, state->obstacles, state->reachable))
			{
			case 0:
				break;

			case ERROR_MISSING:
				return INPUT_IGNORE;

			default:
				return status;
			}
		}

		// TODO init distances for reachable locations for the current pawn
		return 0;
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

	if (code == EVENT_MOUSE_LEFT)
	{
		struct pawn *selected = state->pawn;
		struct pawn *new = battle->field[y][x].pawn;

		// If there is a selected pawn, put it at the clicked field.
		// If there is a pawn at the clicked field, take it from there.

		if (selected)
		{
			size_t i = 0;
			for(i = 0; i < state->reachable_count; ++i)
				if ((state->reachable[i].x == x) && (state->reachable[i].y == y))
					goto reachable;
			return INPUT_IGNORE;

reachable:
			selected->position = (struct position){x, y};
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

	return INPUT_IGNORE;
}

static int input_ignore(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_animation *state = argument;

	// TODO finish animation when there is nothing left to show

	// TODO there should be no input required to finish the animation
	if (animation_progress(&state->start, ANIMATION_DURATION) >= 1)
		return INPUT_FINISH;

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
			.left = BUTTON_ENTER_X,
			.right = BUTTON_ENTER_X + BUTTON_WIDTH,
			.top = BUTTON_ENTER_Y,
			.bottom = BUTTON_ENTER_Y + BUTTON_HEIGHT,
			.callback = input_finish
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
	//state.hover = POINT_NONE;

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
			.left = BUTTON_ENTER_X,
			.right = BUTTON_ENTER_X + BUTTON_WIDTH,
			.top = BUTTON_ENTER_Y,
			.bottom = BUTTON_ENTER_Y + BUTTON_HEIGHT,
			.callback = input_finish
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
	state.field = 0;
	state.pawn = 0;
	//state.hover = POINT_NONE;

	state.obstacles = obstacles;
	state.graph = graph;

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_battle, game, &state);
}

int input_animation(const struct game *restrict game, const struct battle *restrict battle, struct position (*movements)[MOVEMENT_STEPS + 1])
{
	struct area areas[] = {
		{
			.left = 0,
			.right = SCREEN_WIDTH - 1,
			.top = 0,
			.bottom = SCREEN_HEIGHT - 1,
			.callback = input_ignore
		},
	};

	struct state_animation state = {0};
	state.battle = battle;
	state.movements = movements;
	state.animation_duration = ANIMATION_DURATION;

	gettimeofday(&state.start, 0);

	// Mark each field that is traversed by a pawn (used to display gates as open).
	size_t i, j;
	for(i = 0; i < battle->pawns_count; ++i)
	{
		for(j = 0; j <= MOVEMENT_STEPS; ++j)
		{
			struct position p = movements[i][j];
			state.traversed[(size_t)(p.x + 0.5)][(size_t)(p.y + 0.5)] = 1;
		}
	}

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_animation, game, &state);
}

int input_animation_shoot(const struct game *restrict game, const struct battle *restrict battle)
{
	struct area areas[] = {
		{
			.left = 0,
			.right = SCREEN_WIDTH - 1,
			.top = 0,
			.bottom = SCREEN_HEIGHT - 1,
			.callback = input_ignore
		},
	};

	struct state_animation state = {0};
	state.battle = battle;
	state.animation_duration = ANIMATION_SHOOT_DURATION;

	gettimeofday(&state.start, 0);

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_animation_shoot, game, &state);
}
