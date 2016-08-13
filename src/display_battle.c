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

#include <stdlib.h>
#include <unistd.h>

#define GL_GLEXT_PROTOTYPES

#include <GL/glx.h>

#include "format.h"
#include "game.h"
#include "draw.h"
#include "map.h"
#include "pathfinding.h"
#include "movement.h"
#include "battle.h"
#include "combat.h"
#include "image.h"
#include "interface.h"
#include "display_common.h"
#include "input.h"
#include "input_battle.h"
#include "display_battle.h"

#define S(s) (s), sizeof(s) - 1

#define PLAYER_INDICATOR_RADIUS 10

#define BATTLEFIELD_X(x) (unsigned)(BATTLE_X + ((x) - PAWN_RADIUS) * FIELD_SIZE + 0.5)
#define BATTLEFIELD_Y(y) (unsigned)(BATTLE_Y + ((y) - PAWN_RADIUS) * FIELD_SIZE + 0.5)

extern Display *display;
extern GLXDrawable drawable;

// TODO Create a struct that stores all the information about the battle (battlefield, players, etc.)
struct battle *battle;

void if_set(struct battle *b)
{
	battle = b;
}

static void if_battlefield(unsigned char player, const struct game *game, const struct battle *restrict battle, const unsigned char open[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	size_t x, y;

	// Display battlefield background.
	display_image(&image_terrain[0], BATTLE_X - 8, BATTLE_Y - 8, BATTLEFIELD_WIDTH * FIELD_SIZE + 16, BATTLEFIELD_HEIGHT * FIELD_SIZE + 16);

	// Draw rectangle with current player's color.
	fill_rectangle(CTRL_X, CTRL_Y, 256, 16, display_colors[Player + player]);

	// Draw the control section in gray.
	fill_rectangle(CTRL_X, CTRL_Y + CTRL_MARGIN, CTRL_WIDTH, CTRL_HEIGHT - CTRL_MARGIN, display_colors[Gray]);

	// Display battlefield obstacles.
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			const struct battlefield *field = &battle->field[y][x];

			if (field->blockage && !open[y][x]) // TODO change this when there is an image for open gate
			{
				const struct image *image;

				// TODO use separate images for palisade and fortress
				if (field->owner == OWNER_NONE) image = &image_palisade[field->blockage_location];
				else
				{
					if (field->blockage_location == (POSITION_LEFT | POSITION_RIGHT))
						image = &image_palisade_gate[0];
					else // field->blockage_location == (POSITION_TOP | POSITION_BOTTOM)
						image = &image_palisade_gate[1];
				}

				image_draw(image, BATTLE_X + x * object_group[Battlefield].width, BATTLE_Y + y * object_group[Battlefield].height);
			}
		}

	// TODO towers
}

void if_animation(const void *argument, const struct game *game, double progress)
{
	const struct state_animation *state = argument;

	size_t p;

	size_t step = (unsigned)(progress * MOVEMENT_STEPS);
	progress = progress * MOVEMENT_STEPS - step;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if_battlefield(PLAYER_NEUTRAL, game, battle, state->traversed); // TODO change first argument

	// pawns
	for(p = 0; p < battle->pawns_count; ++p)
	{
		struct pawn *pawn = battle->pawns + p;
		double x, y;

		if (!pawn->count) continue;

		x = state->movements[p][step].x * (1 - progress) + state->movements[p][step + 1].x * progress;
		y = state->movements[p][step].y * (1 - progress) + state->movements[p][step + 1].y * progress;

		display_troop(pawn->troop->unit->index, BATTLEFIELD_X(x), BATTLEFIELD_Y(y), Player + pawn->troop->owner, 0, 0);
	}

	glFlush();
	glXSwapBuffers(display, drawable);
}

void if_animation_shoot(const void *argument, const struct game *game, double progress)
{
	const struct state_animation *state = argument;

	size_t p;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	if_battlefield(PLAYER_NEUTRAL, game, battle, state->traversed); // TODO change first argument

	// pawns
	for(p = 0; p < battle->pawns_count; ++p)
	{
		struct pawn *pawn = battle->pawns + p;
		if (!pawn->count) continue;
		display_troop(pawn->troop->unit->index, BATTLEFIELD_X(pawn->position.x), BATTLEFIELD_Y(pawn->position.y), Player + pawn->troop->owner, 0, 0);
	}

	// arrows
	for(p = 0; p < battle->pawns_count; ++p)
	{
		struct pawn *pawn = battle->pawns + p;
		struct position origin, target;
		double dx, dy;
		struct image *image;
		double x, y;

		if (!pawn->count || (pawn->action != ACTION_SHOOT)) continue;

		// Determine arrows direction.
		origin = pawn->position;
		target = pawn->target.position;
		dx = fabs(target.x - origin.x);
		dy = fabs(target.y - origin.y);
		if (dx >= dy)
		{
			if (target.x > origin.x) image = &image_shoot_right;
			else image = &image_shoot_left;
		}
		else
		{
			if (target.y > origin.y) image = &image_shoot_down;
			else image = &image_shoot_up;
		}

		// Determine arrows position and draw image.
		x = origin.x * (1.0 - progress) + target.x * progress;
		y = origin.y * (1.0 - progress) + target.y * progress;
		image_draw(image, BATTLEFIELD_X(x), BATTLEFIELD_Y(y));
	}

	glFlush();
	glXSwapBuffers(display, drawable);
}

// TODO rewrite this
static void if_formation_players(const struct game *restrict game, const struct battle *restrict battle, unsigned char player)
{
	size_t i;

	for(i = 0; i < game->players_count; ++i)
	{
		const struct pawn *restrict pawn;
		const double *position;

		struct tile reachable[REACHABLE_LIMIT];
		size_t reachable_count;

		if (i == player) continue;
		if (!battle->players[i].pawns_count) continue;

		pawn = battle->players[i].pawns[0];

		if (battle->assault)
		{
			reachable_count = formation_reachable_assault(game, battle, pawn, reachable);

			if (pawn->startup == NEIGHBOR_GARRISON) position = formation_position_garrison;
			else position = formation_position_assault[pawn->startup];
		}
		else
		{
			reachable_count = formation_reachable_open(game, battle, pawn, reachable);

			if (pawn->startup == NEIGHBOR_SELF) position = formation_position_defend;
			else position = formation_position_attack[pawn->startup];
		}

		fill_circle(BATTLE_X + position[0] * object_group[Battlefield].width, BATTLE_Y + position[1] * object_group[Battlefield].height, PLAYER_INDICATOR_RADIUS, Player + pawn->troop->owner);
	}
}

void if_formation(const void *argument, const struct game *game)
{
	// TODO battle must be passed as argument

	const struct state_formation *state = argument;
	const unsigned char open[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH] = {0};

	size_t i, j;

	if_battlefield(state->player, game, battle, open);

	// Indicate where on the battlefield there will be pawns of other players.
	if_formation_players(game, battle, state->player);

	struct pawn *const *pawns = battle->players[state->player].pawns;
	size_t pawns_count = battle->players[state->player].pawns_count;
	for(i = 0; i < pawns_count; ++i)
	{
		const struct troop *troop = pawns[i]->troop;

		if (pawns[i] == state->pawn)
		{
			// Display at which fields the pawn can be placed.
			for(j = 0; j < state->reachable_count; ++j)
				if (!battle_field(battle, state->reachable[j])->pawn)
					fill_rectangle(BATTLE_X + state->reachable[j].x * object_group[Battlefield].width, BATTLE_Y + state->reachable[j].y * object_group[Battlefield].height, object_group[Battlefield].width, object_group[Battlefield].height, display_colors[FieldReachable]);

			// Display the selected pawn in the control section.
			fill_rectangle(CTRL_X, CTRL_Y + CTRL_MARGIN, FIELD_SIZE + MARGIN * 2, FIELD_SIZE + font12.height + MARGIN * 2, display_colors[Self]);
			display_troop(troop->unit->index, CTRL_X + MARGIN, CTRL_Y + CTRL_MARGIN + MARGIN, Player + troop->owner, Black, pawns[i]->count);
		}
		else
		{
			// Display the pawn at its present location.
			display_troop(troop->unit->index, BATTLEFIELD_X(pawns[i]->position.x), BATTLEFIELD_Y(pawns[i]->position.y), Player + state->player, 0, 0);
		}
	}

	show_button(S("Start battle"), BUTTON_ENTER_X, BUTTON_ENTER_Y);

	// Display hovered field in color.
	// TODO this is buggy
	/*if (!point_eq(state->hover, POINT_NONE))
		fill_rectangle(BATTLE_X + state->hover.x * object_group[Battlefield].width, BATTLE_Y + state->hover.y * object_group[Battlefield].height, object_group[Battlefield].width, object_group[Battlefield].height, display_colors[Hover]);*/
}

static void show_health(const struct pawn *pawn, unsigned x, unsigned y)
{
	char buffer[32], *end; // TODO make sure this is enough

	unsigned total = pawn->troop->unit->health * pawn->count;
	unsigned left = total - pawn->hurt;

	end = format_bytes(buffer, S("health: "));
	end = format_uint(end, left, 10);
	end = format_bytes(end, S(" / "));
	end = format_uint(end, total, 10);

	draw_string(buffer, end - buffer, x, y, &font12, White);

	// HEALTH_BAR * left / total
	// HEALTH_BAR * total / total == HEALTH_BAR
	//fill_rectangle(CTRL_X, CTRL_Y + CTRL_MARGIN, FIELD_SIZE + MARGIN * 2, FIELD_SIZE + font12.height + MARGIN * 2, display_colors[color]);
	//
}

static void show_strength(const struct battlefield *field, unsigned x, unsigned y)
{
	char buffer[32], *end; // TODO make sure this is enough

	end = format_bytes(buffer, S("strength: "));
	end = format_uint(end, field->strength, 10);

	draw_string(buffer, end - buffer, x, y, &font12, White);
}

static void if_battle_pawn(const struct game *game, const struct state_battle *restrict state, const struct pawn *restrict pawn)
{
	// TODO fix this
	// Show pawn movement target.
	struct position position = pawn->position, next;
	for(size_t i = 0; i < pawn->path.count; ++i)
	{
		struct point from, to;

		next = pawn->path.data[i];

		from.x = (int)(position.x * FIELD_SIZE + 0.5);
		from.y = (int)(position.y * FIELD_SIZE + 0.5);

		to.x = (int)(next.x * FIELD_SIZE + 0.5);
		to.y = (int)(next.y * FIELD_SIZE + 0.5);

		//if (pawn->moves[i].time <= 1.0) color = PathReachable;
		//else color = PathUnreachable;
		//display_arrow(from, to, BATTLE_X, BATTLE_Y, color);
		display_arrow(from, to, BATTLE_X, BATTLE_Y, PathReachable);

		position = next;
	}

	switch (pawn->action)
	{
		struct position target;
	case ACTION_SHOOT:
		target = pawn->target.position;
		image_draw(&image_pawn_shoot, BATTLEFIELD_X(target.x), BATTLEFIELD_Y(target.y));
		break;

	case ACTION_FIGHT:
		target = pawn->target.pawn->position;
		image_draw(&image_pawn_fight, BATTLEFIELD_X(target.x), BATTLEFIELD_Y(target.y));
		break;

	case ACTION_ASSAULT:
		target = (struct position){pawn->target.field->tile.x, pawn->target.field->tile.y};
		image_draw(&image_pawn_assault, BATTLEFIELD_X(target.x + 0.5), BATTLEFIELD_Y(target.y + 0.5));
		break;
	}
}

void if_battle(const void *argument, const struct game *game)
{
	// TODO battle must be passed as argument

	const struct state_battle *state = argument;
	const unsigned char open[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH] = {0};

	const struct pawn *pawn;
	enum color color;

	if_battlefield(state->player, game, battle, open);

	// TODO indicate which pawn/field is hovered?

	if ((pawn = state->pawn) && (pawn->troop->owner == state->player))
	{
		// Show which tiles are reachable by the selected pawn.
		// TODO maybe only do this if there is no pawn action?
		for(size_t y = 0; y < BATTLEFIELD_HEIGHT; ++y)
			for(size_t x = 0; x < BATTLEFIELD_WIDTH; ++x)
				if (state->reachable[y][x] <= pawn->troop->unit->speed)
				{
					// If a tile is only partially reachable, color it with less opaqueness.
					double distance_left = pawn->troop->unit->speed - state->reachable[y][x];
					double opaqueness = ((distance_left >= M_SQRT2 / 2) ? 1.0 : distance_left / (M_SQRT2 / 2));
					unsigned char color[4] = {0, 0, 0, 64 * opaqueness};
					fill_rectangle(BATTLE_X + x * object_group[Battlefield].width, BATTLE_Y + y * object_group[Battlefield].height, object_group[Battlefield].width, object_group[Battlefield].height, color);
				}
	}

	// Display pawns.
	for(size_t i = 0; i < battle->pawns_count; ++i)
	{
		pawn = battle->pawns + i;
		if (!pawn->count) continue;

		if (pawn->troop->owner == state->player) color = Self;
		else if (game->players[pawn->troop->owner].alliance == game->players[state->player].alliance) color = Ally;
		else color = Enemy;

		display_troop(pawn->troop->unit->index, BATTLEFIELD_X(pawn->position.x), BATTLEFIELD_Y(pawn->position.y), color, 0, 0);
	}

	// Display information about the selected pawn or field (or all pawns if nothing is selected).
	if (pawn = state->pawn)
	{
		// Indicate that the pawn is selected.
		image_draw(&image_selected, BATTLEFIELD_X(pawn->position.x) - 1, BATTLEFIELD_Y(pawn->position.y) - 1);

		// Display pawn information in the control section.
		if (pawn->troop->owner == state->player) color = Self;
		else if (allies(game, state->player, pawn->troop->owner)) color = Ally;
		else color = Enemy;
		fill_rectangle(CTRL_X, CTRL_Y + CTRL_MARGIN, FIELD_SIZE + MARGIN * 2, FIELD_SIZE + font12.height + MARGIN * 2, display_colors[color]);
		display_troop(pawn->troop->unit->index, CTRL_X + MARGIN, CTRL_Y + CTRL_MARGIN + MARGIN, Player + pawn->troop->owner, Black, pawn->count);

		show_health(pawn, CTRL_X, CTRL_Y + CTRL_MARGIN + FIELD_SIZE + font12.height + MARGIN * 2 + MARGIN);

		if (pawn->troop->owner == state->player)
			if_battle_pawn(game, state, pawn);
	}
	else if (state->field)
	{
		if (state->field->blockage == BLOCKAGE_OBSTACLE)
			show_strength(state->field, CTRL_X, CTRL_Y + CTRL_MARGIN);
	}
	else for(size_t i = 0; i < battle->players[state->player].pawns_count; ++i)
	{
		if (!battle->players[state->player].pawns[i]->count) continue;
		if_battle_pawn(game, state, battle->players[state->player].pawns[i]);
	}

	show_button(S("Ready"), BUTTON_ENTER_X, BUTTON_ENTER_Y);

	// TODO finish this test
	/*{
		// http://xcb.freedesktop.org/tutorial/mousecursors/
		// https://github.com/eBrnd/i3lock-color/blob/master/xcb.c	  create_cursor()
		// http://xcb-util.sourcearchive.com/documentation/0.3.3-1/group__xcb____image__t_g029605b47d6ab95eac66b125a9a7dd64.html
		// https://en.wikipedia.org/wiki/X_BitMap#Format

		xcb_pixmap_t bitmap;
		xcb_pixmap_t mask;
		xcb_cursor_t cursor;

		uint32_t width = 32, height = 32;

		// in the example: curs_bits is unsigned char [50]
		// width and height are 11 and 19
		// The bitmap data is assumed to be in xbm format (i.e., 8-bit scanline unit, LSB-first, 8-bit pad). If depth is greater than 1, the bitmap will be expanded to a pixmap using the given foreground and background pixels fg and bg.

		unsigned char curs_bits[...];
		unsigned char mask_bits[...];

		bitmap = xcb_create_pixmap_from_bitmap_data(connection, window,
													curs_bits,
													width, height,
													1,
													screen->white_pixel, screen->black_pixel,
													0);
		mask = xcb_create_pixmap_from_bitmap_data(connection, window,
												  mask_bits,
												  width, height,
												  1,
												  screen->white_pixel, screen->black_pixel,
												  0);

		cursor = xcb_generate_id(connection);
		xcb_create_cursor(connection,
						  cursor,
						  bitmap,
						  mask,
						  65535, 65535, 65535,
						  0, 0, 0,
						  0, 0);

		xcb_free_pixmap(connection, bitmap);
		xcb_free_pixmap(connection, mask);
	}*/

#if defined(DEBUG)
	/*for(size_t y = 0; y < BATTLEFIELD_HEIGHT; ++y)
	{
		for(size_t x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			char buffer[6], *end = buffer;
			end = format_uint(end, x, 10);
			*end++ = ',';
			end = format_uint(end, y, 10);
			draw_string(buffer, end - buffer, BATTLE_X + x * object_group[Battlefield].width, BATTLE_Y + y * object_group[Battlefield].height, &font9, White);
		}
	}*/
#endif
}
