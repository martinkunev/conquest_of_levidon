#include <X11/keysym.h>

#include "errors.h"
#include "base.h"
#include "format.h"
#include "map.h"
#include "interface.h"
#include "input.h"
#include "input_report.h"
#include "interface_report.h"
#include "pathfinding.h"
#include "battle.h"

static int input_end(int code, unsigned x, unsigned y, uint16_t modifiers, const struct game *restrict game, void *argument)
{
	switch (code)
	{
	case XK_Escape:
		return INPUT_FINISH;

	default:
		return INPUT_IGNORE;
	}
}

int input_report(const struct game *restrict game, const struct battle *restrict battle)
{
	struct area areas[] = {
		{
			.left = 0,
			.right = SCREEN_WIDTH - 1,
			.top = 0,
			.bottom = SCREEN_HEIGHT - 1,
			.callback = input_end
		},
	};

	struct state_report state;
	state.game = game;
	state.battle = battle;

	return input_local(areas, sizeof(areas) / sizeof(*areas), if_report, 0, &state);
}
