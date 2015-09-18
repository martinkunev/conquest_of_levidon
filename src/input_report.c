#include <X11/keysym.h>

#include "errors.h"
#include "base.h"
#include "format.h"
#include "map.h"
#include "pathfinding.h"
#include "interface.h"
#include "input.h"
#include "input_report.h"
#include "interface_common.h"
#include "interface_report.h"
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

int input_report_battle(const struct game *restrict game, const struct battle *restrict battle)
{
	struct area areas[] = {
		{
			.left = 0,
			.right = SCREEN_WIDTH - 1,
			.top = 0,
			.bottom = SCREEN_HEIGHT - 1,
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

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_report_battle, 0, &state);
}

int input_report_map(const struct game *restrict game)
{
	struct area areas[] = {
		{
			.left = 0,
			.right = SCREEN_WIDTH - 1,
			.top = 0,
			.bottom = SCREEN_HEIGHT - 1,
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
