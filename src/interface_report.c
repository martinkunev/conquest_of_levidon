#include <GL/glx.h>

#include "base.h"
#include "map.h"
#include "input_report.h"
#include "interface_report.h"
#include "pathfinding.h"
#include "battle.h"
#include "display.h"

#define S(s) (s), sizeof(s) - 1

#define REPORT_BEFORE_X 32
#define REPORT_AFTER_X 416

#define TITLE_Y 32
#define REPORT_Y 64

#define MARGIN_X 40
#define MARGIN_Y 60

extern Display *display;
extern GLXDrawable drawable;

extern struct font font12;

void if_report(const void *argument, const struct game *game_)
{
	const struct state_report *restrict state = argument;
	const struct game *restrict game = state->game;
	const struct battle *restrict battle = state->battle;

	size_t player;
	size_t i;

	unsigned offset[PLAYERS_LIMIT] = {0};
	unsigned position_before[PLAYERS_LIMIT] = {0};
	unsigned position_after[PLAYERS_LIMIT] = {0};
	unsigned offset_next = REPORT_Y;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// TODO somehow indicate which players are allies

	// TODO for assault display whether the assault was successful, etc. (what is the damage to the garrison)

	draw_string(S("Before the battle"), REPORT_BEFORE_X, TITLE_Y, &font12, White);
	draw_string(S("After the battle"), REPORT_AFTER_X, TITLE_Y, &font12, White);

	for(player = 0; player < game->players_count; ++player)
	{
		for(i = 0; i < battle->players[player].pawns_count; ++i)
		{
			const struct pawn *restrict pawn = battle->players[player].pawns[i];
			unsigned char owner = pawn->troop->owner;

			if (!offset[owner])
			{
				offset[owner] = offset_next;
				offset_next += MARGIN_Y;

				position_before[owner] = REPORT_BEFORE_X;
				position_after[owner] = REPORT_AFTER_X;
			}

			display_troop(pawn->troop->unit->index, position_before[owner], offset[owner], Player + owner, White, pawn->troop->count);
			position_before[owner] += MARGIN_X;

			display_troop(pawn->troop->unit->index, position_after[owner], offset[owner], Player + owner, White, pawn->count);
			position_after[owner] += MARGIN_X;
		}
	}

	glFlush();
	glXSwapBuffers(display, drawable);
}
