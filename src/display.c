#include <sys/time.h>
#include <unistd.h>

#define GL_GLEXT_PROTOTYPES

#include <GL/glx.h>
#include <GL/glext.h>

#include <xcb/xcb.h>

#include "format.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "movement.h"
#include "interface.h"
#include "image.h"
#include "input.h"
//#include "input_map.h"
#include "input_battle.h"
#include "display.h"

#define S(s) (s), sizeof(s) - 1

#define ANIMATION_DURATION 3.0

#define PREFIX_IMG PREFIX "share/conquest_of_levidon/img/"

extern Display *display;
extern GLXDrawable drawable;

// TODO Create a struct that stores all the information about the battle (battlefield, players, etc.)
struct battle *battle;

struct image image_selected, image_assault, image_flag, image_flag_small, image_panel, image_construction, image_movement;
struct image image_pawn_fight, image_pawn_assault, image_pawn_shoot;
struct image image_terrain[1];
struct image image_garrison[2]; // TODO this must be big enough for all garrison types
struct image image_map_village, image_map_garrison[2]; // TODO this must be big enough for all garrison types
struct image image_gold, image_food, image_wood, image_stone, image_iron, image_time;
struct image image_scroll_left, image_scroll_right;
struct image image_units[7]; // TODO the array must be enough to hold units_count units
struct image image_buildings[13]; // TODO the array must be big enough to hold buildings_count elements
struct image image_buildings_gray[13]; // TODO the array must be big enough to hold buildings_count elements
struct image image_palisade[16], image_palisade_gate[2], image_fortress[16], image_fortress_gate[2];

void if_load_images(void)
{
	image_load_png(&image_selected, PREFIX_IMG "selected.png", 0);
	image_load_png(&image_assault, PREFIX_IMG "assault.png", 0);
	image_load_png(&image_flag, PREFIX_IMG "flag.png", 0);
	image_load_png(&image_flag_small, PREFIX_IMG "flag_small.png", 0);
	image_load_png(&image_panel, PREFIX_IMG "panel.png", 0);
	image_load_png(&image_construction, PREFIX_IMG "construction.png", 0);
	image_load_png(&image_movement, PREFIX_IMG "movement.png", 0);

	image_load_png(&image_pawn_fight, PREFIX_IMG "pawn_fight.png", 0);
	image_load_png(&image_pawn_assault, PREFIX_IMG "pawn_assault.png", 0);
	image_load_png(&image_pawn_shoot, PREFIX_IMG "pawn_shoot.png", 0);

	image_load_png(&image_garrison[PALISADE], PREFIX_IMG "garrison_palisade.png", 0);
	image_load_png(&image_garrison[FORTRESS], PREFIX_IMG "garrison_fortress.png", 0);

	image_load_png(&image_map_village, PREFIX_IMG "map_village.png", 0);
	image_load_png(&image_map_garrison[PALISADE], PREFIX_IMG "map_palisade.png", 0);
	image_load_png(&image_map_garrison[FORTRESS], PREFIX_IMG "map_fortress.png", 0);

	image_load_png(&image_scroll_left, PREFIX_IMG "scroll_left.png", 0);
	image_load_png(&image_scroll_right, PREFIX_IMG "scroll_right.png", 0);

	image_load_png(&image_gold, PREFIX_IMG "gold.png", 0);
	image_load_png(&image_food, PREFIX_IMG "food.png", 0);
	image_load_png(&image_wood, PREFIX_IMG "wood.png", 0);
	image_load_png(&image_stone, PREFIX_IMG "stone.png", 0);
	image_load_png(&image_iron, PREFIX_IMG "iron.png", 0);
	image_load_png(&image_time, PREFIX_IMG "time.png", 0);

	image_load_png(&image_units[UnitPeasant], PREFIX_IMG "peasant.png", 0);
	image_load_png(&image_units[UnitMilitia], PREFIX_IMG "militia.png", 0);
	image_load_png(&image_units[UnitPikeman], PREFIX_IMG "pikeman.png", 0);
	image_load_png(&image_units[UnitArcher], PREFIX_IMG "archer.png", 0);
	image_load_png(&image_units[UnitLongbow], PREFIX_IMG "longbow.png", 0);
	image_load_png(&image_units[UnitLightCavalry], PREFIX_IMG "light_cavalry.png", 0);
	image_load_png(&image_units[UnitBatteringRam], PREFIX_IMG "battering_ram.png", 0);

	image_load_png(&image_buildings[0], PREFIX_IMG "farm.png", 0);
	image_load_png(&image_buildings[1], PREFIX_IMG "irrigation.png", 0);
	image_load_png(&image_buildings[2], PREFIX_IMG "sawmill.png", 0);
	image_load_png(&image_buildings[3], PREFIX_IMG "mine.png", 0);
	image_load_png(&image_buildings[4], PREFIX_IMG "blast_furnace.png", 0);
	image_load_png(&image_buildings[5], PREFIX_IMG "barracks.png", 0);
	image_load_png(&image_buildings[6], PREFIX_IMG "archery_range.png", 0);
	image_load_png(&image_buildings[7], PREFIX_IMG "stables.png", 0);
	image_load_png(&image_buildings[8], PREFIX_IMG "watch_tower.png", 0);
	image_load_png(&image_buildings[9], PREFIX_IMG "palisade.png", 0);
	image_load_png(&image_buildings[10], PREFIX_IMG "fortress.png", 0);
	image_load_png(&image_buildings[11], PREFIX_IMG "workshop.png", 0);
	image_load_png(&image_buildings[12], PREFIX_IMG "forge.png", 0);

	image_load_png(&image_buildings_gray[0], PREFIX_IMG "farm.png", 1);
	image_load_png(&image_buildings_gray[1], PREFIX_IMG "irrigation.png", 1);
	image_load_png(&image_buildings_gray[2], PREFIX_IMG "sawmill.png", 1);
	image_load_png(&image_buildings_gray[3], PREFIX_IMG "mine.png", 1);
	image_load_png(&image_buildings_gray[4], PREFIX_IMG "blast_furnace.png", 1);
	image_load_png(&image_buildings_gray[5], PREFIX_IMG "barracks.png", 1);
	image_load_png(&image_buildings_gray[6], PREFIX_IMG "archery_range.png", 1);
	image_load_png(&image_buildings_gray[7], PREFIX_IMG "stables.png", 1);
	image_load_png(&image_buildings_gray[8], PREFIX_IMG "watch_tower.png", 1);
	image_load_png(&image_buildings_gray[9], PREFIX_IMG "palisade.png", 1);
	image_load_png(&image_buildings_gray[10], PREFIX_IMG "fortress.png", 1);
	image_load_png(&image_buildings_gray[11], PREFIX_IMG "workshop.png", 1);
	image_load_png(&image_buildings_gray[12], PREFIX_IMG "forge.png", 1);

	image_load_png(&image_terrain[0], PREFIX_IMG "terrain_grass.png", 0);

	// Load battlefield images.
	size_t i;
	for(i = 1; i < 16; ++i) // TODO fix this
	{
		char buffer[64], *end; // TODO make sure this is enough

		end = format_bytes(buffer, S(PREFIX_IMG "palisade"));
		end = format_uint(end, i, 10);
		end = format_bytes(end, S(".png"));
		*end = 0;
		image_load_png(&image_palisade[i], buffer, 0);

		end = format_bytes(buffer, S(PREFIX_IMG "fortress"));
		end = format_uint(end, i, 10);
		end = format_bytes(end, S(".png"));
		*end = 0;
		image_load_png(&image_fortress[i], buffer, 0);
	}
	image_load_png(&image_palisade_gate[0], PREFIX_IMG "palisade_gate0.png", 0);
	image_load_png(&image_fortress_gate[0], PREFIX_IMG "fortress_gate0.png", 0);
	image_load_png(&image_palisade_gate[1], PREFIX_IMG "palisade_gate1.png", 0);
	image_load_png(&image_fortress_gate[1], PREFIX_IMG "fortress_gate1.png", 0);
}

void display_troop(size_t unit, unsigned x, unsigned y, enum color color, enum color text, unsigned count)
{
	fill_rectangle(x, y, FIELD_SIZE, FIELD_SIZE, color);
	image_draw(&image_units[unit], x, y);

	if (count)
	{
		char buffer[16];
		size_t length = format_uint(buffer, count, 10) - (uint8_t *)buffer;
		draw_string(buffer, length, x + (FIELD_SIZE - (length * 10)) / 2, y + FIELD_SIZE, &font12, text);
	}
}

// TODO write this better
static int if_animation(const struct player *restrict players, const struct battle *restrict battle, double progress, unsigned char traversed[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH])
{
	int finished = 1;

	size_t x, y;
	size_t p;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Display panel background pattern.
	display_image(&image_terrain[0], BATTLE_X - 8, BATTLE_Y - 8, BATTLEFIELD_WIDTH * FIELD_SIZE + 16, BATTLEFIELD_HEIGHT * FIELD_SIZE + 16);

	// battlefield
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			const struct battlefield *field = &battle->field[y][x];
			if (field->blockage && !traversed[y][x]) // TODO change this when there is an image for open gate
			{
				// TODO decide whether to use palisade or fortress

				const struct image *image;

				if (field->owner == OWNER_NONE) image = &image_palisade[field->position];
				else
				{
					if (field->position == (POSITION_LEFT | POSITION_RIGHT))
						image = &image_palisade_gate[0];
					else // field->position == (POSITION_TOP | POSITION_BOTTOM)
						image = &image_palisade_gate[1];
				}

				image_draw(image, BATTLE_X + x * object_group[Battlefield].width, BATTLE_Y + y * object_group[Battlefield].height);
			}
		}

	// pawns
	for(p = 0; p < battle->pawns_count; ++p)
	{
		struct pawn *pawn = battle->pawns + p;
		double x, y, x_final, y_final;

		if (!pawn->count) continue;

		// If the pawn will move more this round, the animation must continue.
		movement_location(pawn, progress, &x, &y);
		movement_location(pawn, 1.0, &x_final, &y_final);
		if ((x != x_final) || (y != y_final)) finished = 0;

		display_troop(pawn->troop->unit->index, BATTLE_X + x * object_group[Battlefield].width, BATTLE_Y + y * object_group[Battlefield].height, Player + pawn->troop->owner, 0, 0);
	}

	glFlush();
	glXSwapBuffers(display, drawable);

	return finished;
}
static inline unsigned long timediff(const struct timeval *restrict end, const struct timeval *restrict start)
{
	return (end->tv_sec * 1000000 + end->tv_usec - start->tv_sec * 1000000 - start->tv_usec);
}
void input_animation(const struct game *restrict game, const struct battle *restrict battle)
{
	struct timeval start, now;
	double progress;

	// Mark each field that is traversed by a pawn (used to display gates as open).
	unsigned char traversed[BATTLEFIELD_HEIGHT][BATTLEFIELD_WIDTH] = {0};
	size_t i, j;
	for(i = 0; i < battle->pawns_count; ++i)
	{
		struct point visited[UNIT_SPEED_LIMIT];
		unsigned visited_count = movement_visited(battle->pawns + i, visited);
		for(j = 0; j < visited_count; ++j)
			traversed[visited[j].y][visited[j].x] = 1;
	}

	gettimeofday(&start, 0);
	do
	{
		gettimeofday(&now, 0);
		progress = timediff(&now, &start) / (ANIMATION_DURATION * 1000000.0);
		if (if_animation(game->players, battle, progress, traversed))
			break;
	} while (progress < 1.0);
}

static void if_battlefield(unsigned char player, const struct game *game)
{
	// Display battleifled background.
	display_image(&image_terrain[0], BATTLE_X - 8, BATTLE_Y - 8, BATTLEFIELD_WIDTH * FIELD_SIZE + 16, BATTLEFIELD_HEIGHT * FIELD_SIZE + 16);

	// Draw rectangle with current player's color.
	fill_rectangle(CTRL_X, CTRL_Y, 256, 16, Player + player);

	// Draw the control section in gray.
	fill_rectangle(CTRL_X, CTRL_Y + CTRL_MARGIN, CTRL_WIDTH, CTRL_HEIGHT - CTRL_MARGIN, Gray);
}

void if_formation(const void *argument, const struct game *game)
{
	// TODO battle must be passed as argument

	const struct state_formation *state = argument;

	size_t x, y;

	if_battlefield(state->player, game);

	// TODO mark somehow that only self pawns are displayed

	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			const struct battlefield *field = &battle->field[y][x];
			if (field->blockage)
			{
				// TODO decide whether to use palisade or fortress

				const struct image *image;

				if (field->owner == OWNER_NONE) image = &image_palisade[field->position];
				else
				{
					if (field->position == (POSITION_LEFT | POSITION_RIGHT))
						image = &image_palisade_gate[0];
					else // field->position == (POSITION_TOP | POSITION_BOTTOM)
						image = &image_palisade_gate[1];
				}

				image_draw(image, BATTLE_X + x * object_group[Battlefield].width, BATTLE_Y + y * object_group[Battlefield].height);
			}
		}

	size_t i, j;
	struct pawn *const *pawns = battle->players[state->player].pawns;
	size_t pawns_count = battle->players[state->player].pawns_count;
	for(i = 0; i < pawns_count; ++i)
	{
		const struct troop *troop = pawns[i]->troop;

		if (pawns[i] == state->pawn)
		{
			// Display at which fields the pawn can be placed.
			for(j = 0; j < state->reachable_count; ++j)
				if (!battle->field[state->reachable[j].y][state->reachable[j].x].pawn)
					fill_rectangle(BATTLE_X + state->reachable[j].x * object_group[Battlefield].width, BATTLE_Y + state->reachable[j].y * object_group[Battlefield].height, object_group[Battlefield].width, object_group[Battlefield].height, FieldReachable);

			// Display the selected pawn in the control section.
			fill_rectangle(CTRL_X, CTRL_Y + CTRL_MARGIN, FIELD_SIZE + MARGIN * 2, FIELD_SIZE + font12.height + MARGIN * 2, Self);
			display_troop(troop->unit->index, CTRL_X + MARGIN, CTRL_Y + CTRL_MARGIN + MARGIN, Player + troop->owner, Black, pawns[i]->count);
		}
		else
		{
			// Display the pawn at its present location.
			struct point location = pawns[i]->moves[0].location;
			display_troop(troop->unit->index, BATTLE_X + location.x * object_group[Battlefield].width, BATTLE_Y + location.y * object_group[Battlefield].height, Player + state->player, 0, 0);
		}
	}

	// Display hovered field in color.
	// TODO this is buggy
	/*if (!point_eq(state->hover, POINT_NONE))
		fill_rectangle(BATTLE_X + state->hover.x * object_group[Battlefield].width, BATTLE_Y + state->hover.y * object_group[Battlefield].height, object_group[Battlefield].width, object_group[Battlefield].height, Hover);*/
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
	//fill_rectangle(CTRL_X, CTRL_Y + CTRL_MARGIN, FIELD_SIZE + MARGIN * 2, FIELD_SIZE + font12.height + MARGIN * 2, color);
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
	enum color color;
	size_t i;

	// Show pawn movement target.
	for(i = 1; i < pawn->moves_count; ++i)
	{
		struct point from = pawn->moves[i - 1].location;
		from.x = from.x * FIELD_SIZE + FIELD_SIZE / 2;
		from.y = from.y * FIELD_SIZE + FIELD_SIZE / 2;

		struct point to = pawn->moves[i].location;
		to.x = to.x * FIELD_SIZE + FIELD_SIZE / 2;
		to.y = to.y * FIELD_SIZE + FIELD_SIZE / 2;

		if (pawn->moves[i].time <= 1.0) color = PathReachable;
		else color = PathUnreachable;
		display_arrow(from, to, BATTLE_X, BATTLE_Y, color);
	}

	if (pawn->action == PAWN_SHOOT)
	{
		image_draw(&image_pawn_shoot, BATTLE_X + pawn->target.field.x * FIELD_SIZE, BATTLE_Y + pawn->target.field.y * FIELD_SIZE);
	}
	else if (pawn->action == PAWN_FIGHT)
	{
		struct point target = pawn->target.pawn->moves[0].location;
		image_draw(&image_pawn_fight, BATTLE_X + target.x * FIELD_SIZE, BATTLE_Y + target.y * FIELD_SIZE);
	}
	else if (pawn->action == PAWN_ASSAULT)
	{
		struct point target = pawn->target.field;
		image_draw(&image_pawn_assault, BATTLE_X + target.x * FIELD_SIZE, BATTLE_Y + target.y * FIELD_SIZE);
	}
}

void if_battle(const void *argument, const struct game *game)
{
	// TODO battle must be passed as argument

	const struct state_battle *state = argument;

	size_t x, y;
	const struct pawn *pawn;

	if_battlefield(state->player, game);

	// Display pawns and obstacles.
	for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
		for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
		{
			const struct battlefield *field = &battle->field[y][x];
			if (pawn = field->pawn)
			{
				enum color color;
				if (pawn->troop->owner == state->player) color = Self;
				else if (game->players[pawn->troop->owner].alliance == game->players[state->player].alliance) color = Ally;
				else color = Enemy;

				display_troop(pawn->troop->unit->index, BATTLE_X + x * object_group[Battlefield].width, BATTLE_Y + y * object_group[Battlefield].height, color, 0, 0);

				// TODO towers
			}
			else if (field->blockage)
			{
				// TODO decide whether to use palisade or fortress

				const struct image *image;

				if (field->owner == OWNER_NONE) image = &image_palisade[field->position];
				else
				{
					if (field->position == (POSITION_LEFT | POSITION_RIGHT))
						image = &image_palisade_gate[0];
					else // field->position == (POSITION_TOP | POSITION_BOTTOM)
						image = &image_palisade_gate[1];
				}

				image_draw(image, BATTLE_X + x * object_group[Battlefield].width, BATTLE_Y + y * object_group[Battlefield].height);
			}
		}

	// Display hovered field in color.
	// TODO this is buggy
	/*if (!point_eq(state->hover, POINT_NONE))
		fill_rectangle(BATTLE_X + state->hover.x * object_group[Battlefield].width, BATTLE_Y + state->hover.y * object_group[Battlefield].height, object_group[Battlefield].width, object_group[Battlefield].height, Hover);*/

	// Display information about the selected pawn or field (or all pawns if nothing is selected).
	if (pawn = state->pawn)
	{
		enum color color;

		// Indicate that the pawn is selected.
		image_draw(&image_selected, BATTLE_X + state->field.x * FIELD_SIZE - 1, BATTLE_Y + state->field.y * FIELD_SIZE - 1);

		// Display pawn information in the control section.
		if (pawn->troop->owner == state->player) color = Self;
		else if (allies(game, state->player, pawn->troop->owner)) color = Ally;
		else color = Enemy;
		fill_rectangle(CTRL_X, CTRL_Y + CTRL_MARGIN, FIELD_SIZE + MARGIN * 2, FIELD_SIZE + font12.height + MARGIN * 2, color);
		display_troop(pawn->troop->unit->index, CTRL_X + MARGIN, CTRL_Y + CTRL_MARGIN + MARGIN, Player + pawn->troop->owner, Black, pawn->count);

		show_health(pawn, CTRL_X, CTRL_Y + CTRL_MARGIN + FIELD_SIZE + font12.height + MARGIN * 2 + MARGIN);

		if (pawn->troop->owner == state->player)
		{
			if_battle_pawn(game, state, pawn);

			if (!pawn->action)
			{
				// Show which fields are reachable by the pawn.
				for(y = 0; y < BATTLEFIELD_HEIGHT; ++y)
					for(x = 0; x < BATTLEFIELD_WIDTH; ++x)
						if ((state->reachable[y][x] <= pawn->troop->unit->speed) && !battle->field[y][x].pawn)
							fill_rectangle(BATTLE_X + x * object_group[Battlefield].width, BATTLE_Y + y * object_group[Battlefield].height, object_group[Battlefield].width, object_group[Battlefield].height, FieldReachable);
			}
		}
	}
	else if (!point_eq(state->field, POINT_NONE))
	{
		const struct battlefield *restrict field = &battle->field[state->field.y][state->field.x];
		if (field->blockage == BLOCKAGE_OBSTACLE) show_strength(field, CTRL_X, CTRL_Y + CTRL_MARGIN);
	}
	else
	{
		size_t i;
		for(i = 0; i < battle->players[state->player].pawns_count; ++i)
		{
			if (!battle->players[state->player].pawns[i]->count) continue;
			if_battle_pawn(game, state, battle->players[state->player].pawns[i]);
		}
	}

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
}

void if_set(struct battle *b)
{
	battle = b;
}
