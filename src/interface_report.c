#include <GL/glx.h>

#include "base.h"
#include "format.h"
#include "map.h"
#include "interface.h"
#include "input_report.h"
#include "interface_report.h"
#include "pathfinding.h"
#include "battle.h"
#include "display.h"

#define S(s) (s), sizeof(s) - 1

#define REPORT_BEFORE_X 32
#define REPORT_AFTER_X 416

#define MARGIN_X 40
#define MARGIN_Y 60

#define TITLE_SIZE_LIMIT 64

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

	// TODO indicate whether the attacker or the defender wins and which players win/lose/surrender
	// TODO somehow indicate which players are allies
	// TODO for assault display the damage to the garrison

	// Display report title
	char title[TITLE_SIZE_LIMIT], *end = title;
	struct box box;
	if (battle->assault) end = format_bytes(end, S("Assault in "));
	else end = format_bytes(end, S("Open battle in "));
	end = format_bytes(end, battle->region->name, battle->region->name_length);
	box = string_box(title, end - title, &font24);
	draw_string(title, end - title, (SCREEN_WIDTH - box.width) / 2, TITLE_Y, &font24, White);

	draw_string(S("Before the battle"), REPORT_BEFORE_X, LABEL_Y, &font12, White);
	draw_string(S("After the battle"), REPORT_AFTER_X, LABEL_Y, &font12, White);

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

			if (battle->players[player].alive)
			{
				display_troop(pawn->troop->unit->index, position_after[owner], offset[owner], Player + owner, White, pawn->count);
				position_after[owner] += MARGIN_X;
			}
		}
	}
}
