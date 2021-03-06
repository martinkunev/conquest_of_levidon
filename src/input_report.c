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

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#include <X11/keysym.h>

#include "errors.h"
#include "format.h"
#include "game.h"
#include "draw.h"
#include "map.h"
#include "pathfinding.h"
#include "movement.h"
#include "interface.h"
#include "input.h"
#include "input_report.h"
#include "display_common.h"
#include "display_report.h"
#include "battle.h"

static int input_report(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	switch (code)
	{
	case XK_Escape:
		return INPUT_FINISH;

	default:
		return INPUT_IGNORE;
	}
}

static int input_ignore(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	return INPUT_IGNORE;
}

static int input_yes(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_question *restrict state = argument;
	if (code != EVENT_MOUSE_LEFT)
		return INPUT_IGNORE;
	state->region->garrison.reinforce = true;
	return INPUT_FINISH;
}

static int input_no(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	struct state_question *restrict state = argument;
	if (code != EVENT_MOUSE_LEFT)
		return INPUT_IGNORE;
	state->region->garrison.reinforce = false;
	return INPUT_FINISH;
}

int input_report_battle(const struct game *restrict game, const struct battle *restrict battle)
{
	struct area areas[] = {
		{
			.left = 0,
			.right = WINDOW_WIDTH - 1,
			.top = 0,
			.bottom = WINDOW_HEIGHT - 1,
			.callback = input_report,
		},
		{
			.left = BUTTON_EXIT_X,
			.right = BUTTON_EXIT_X + BUTTON_WIDTH,
			.top = BUTTON_EXIT_Y,
			.bottom = BUTTON_EXIT_Y + BUTTON_HEIGHT,
			.callback = input_finish,
		},
	};

	struct state_report state;
	state.game = game;
	state.battle = battle;

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_report_battle, game, &state);
}

int input_report_map(const struct game *restrict game)
{
	struct area areas[] = {
		{
			.left = 0,
			.right = WINDOW_WIDTH - 1,
			.top = 0,
			.bottom = WINDOW_HEIGHT - 1,
			.callback = input_report,
		},
		{
			.left = BUTTON_EXIT_X,
			.right = BUTTON_EXIT_X + BUTTON_WIDTH,
			.top = BUTTON_EXIT_Y,
			.bottom = BUTTON_EXIT_Y + BUTTON_HEIGHT,
			.callback = input_finish,
		},
	};

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_report_map, game, 0);
}

int input_report_players(struct state_report *restrict state)
{
	struct area areas[] = {
		{
			.left = 0,
			.right = WINDOW_WIDTH - 1,
			.top = 0,
			.bottom = WINDOW_HEIGHT - 1,
			.callback = input_report,
		},
		{
			.left = BUTTON_EXIT_X,
			.right = BUTTON_EXIT_X + BUTTON_WIDTH,
			.top = BUTTON_EXIT_Y,
			.bottom = BUTTON_EXIT_Y + BUTTON_HEIGHT,
			.callback = input_finish,
		},
	};

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_report_players, 0, state);
}

int input_report_invasion(const struct game *restrict game, unsigned char player, struct region *restrict region)
{
	struct area areas[] = {
		{
			.left = 0,
			.right = WINDOW_WIDTH - 1,
			.top = 0,
			.bottom = WINDOW_HEIGHT - 1,
			.callback = input_ignore,
		},
		{
			.left = BUTTON_YES_X,
			.right = BUTTON_YES_X + BUTTON_WIDTH - 1,
			.top = BUTTON_YES_Y,
			.bottom = BUTTON_YES_Y + BUTTON_HEIGHT - 1,
			.callback = input_yes,
		},
		{
			.left = BUTTON_NO_X,
			.right = BUTTON_NO_X + BUTTON_WIDTH - 1,
			.top = BUTTON_NO_Y,
			.bottom = BUTTON_NO_Y + BUTTON_HEIGHT - 1,
			.callback = input_no,
		},
	};

	struct state_question state;
	state.game = game;
	state.player = player;
	state.region = region;

	// If playing in hotseat mode, announce which player will be playing.
	if (game->players_local_count >= 2)
	{
		struct state_report state = {
			.title = REPORT_TITLE_NEXT,
			.title_size = sizeof(REPORT_TITLE_NEXT) - 1,
			.players[0] = player,
			.players_count = 1,
		};
		input_report_players(&state);
	}

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_report_invasion, 0, &state);
}
