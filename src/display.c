#include <stdlib.h>
#include <sys/time.h>

#define GL_GLEXT_PROTOTYPES

#include <GL/glx.h>
#include <GL/glext.h>

#include <xcb/xcb.h>

#include "types.h"
#include "format.h"
#include "map.h"
#include "pathfinding.h"
#include "battle.h"
#include "movement.h"
#include "image.h"
#include "input.h"
#include "input_map.h"
#include "input_battle.h"
#include "interface.h"
#include "display.h"

#define S(s) (s), sizeof(s) - 1

#define RESOURCE_GOLD 660
#define RESOURCE_FOOD 680
#define RESOURCE_WOOD 700
#define RESOURCE_IRON 720
#define RESOURCE_STONE 740

#define HEALTH_BAR 64

// TODO compatibility with OpenGL 2.1 (necessary in MacOS X)
#define glGenFramebuffers(...) glGenFramebuffersEXT(__VA_ARGS__)
#define glGenRenderbuffers(...) glGenRenderbuffersEXT(__VA_ARGS__)
#define glBindFramebuffer(...) glBindFramebufferEXT(__VA_ARGS__)
#define glBindRenderbuffer(...) glBindRenderbufferEXT(__VA_ARGS__)
#define glRenderbufferStorage(...) glRenderbufferStorageEXT(__VA_ARGS__)
#define glFramebufferRenderbuffer(...) glFramebufferRenderbufferEXT(__VA_ARGS__)

#define ANIMATION_DURATION 3.0

#define ARROW_LENGTH 60

#define PREFIX_IMG PREFIX "share/medieval/img/"

extern Display *display;
extern GLXDrawable drawable;
extern xcb_screen_t *screen;
extern xcb_connection_t *connection;
extern KeySym *keymap;
extern int keysyms_per_keycode;
extern int keycode_min, keycode_max;

extern struct font font12;

static GLuint map_framebuffer;

static GLuint map_renderbuffer;

// TODO Create a struct that stores all the information about the battle (battlefield, players, etc.)
struct battle *battle;

static struct image image_selected, image_assault, image_flag, image_flag_small, image_panel, image_construction, image_movement;
static struct image image_pawn_fight, image_pawn_assault, image_pawn_shoot;
static struct image image_terrain[1];
static struct image image_garrison[2]; // TODO this must be big enough for all garrison types
static struct image image_map_garrison[2]; // TODO this must be big enough for all garrison types
static struct image image_gold, image_food, image_wood, image_stone, image_iron, image_time;
static struct image image_scroll_left, image_scroll_right;
static struct image image_units[7]; // TODO the array must be enough to hold units_count units
static struct image image_buildings[13]; // TODO the array must be big enough to hold buildings_count elements
static struct image image_buildings_gray[13]; // TODO the array must be big enough to hold buildings_count elements
static struct image image_palisade[16], image_palisade_gate[2], image_fortress[16], image_fortress_gate[2];

static uint8_t *format_sint(uint8_t *buffer, int64_t number)
{
	if (number > 0) *buffer++ = '+';
	return format_int(buffer, number, 10);
}

void if_storage_init(const struct game *game, int width, int height)
{
	size_t i;

	unsigned regions_count = game->regions_count;
	// assert(game->regions_count < 65536);

	glGenRenderbuffers(1, &map_renderbuffer);
	glGenFramebuffers(1, &map_framebuffer);

	glBindRenderbuffer(GL_RENDERBUFFER, map_renderbuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, map_framebuffer);

	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB8, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, map_renderbuffer);

	glViewport(0, 0, width, height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, width, 0, height, 0, 1);

	// TODO why is this necessary
	//glBindRenderbuffer(GL_RENDERBUFFER, 0);
	//glBindFramebuffer(GL_FRAMEBUFFER, 0);
	//glBindFramebuffer(GL_FRAMEBUFFER, map_framebuffer);

	glClear(GL_COLOR_BUFFER_BIT);

	for(i = 0; i < regions_count; ++i)
	{
		glColor3ub(255, i / 256, i % 256);
		display_polygon(game->regions[i].location, 0, 0);
	}

	glFlush();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

int if_storage_get(unsigned x, unsigned y)
{
	GLubyte pixel[3];

	glBindFramebuffer(GL_READ_FRAMEBUFFER, map_framebuffer);
	glReadPixels(x, y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	if (!pixel[0]) return -1;
	return pixel[1] * 256 + pixel[2];
}

void if_storage_term(void)
{
	glDeleteRenderbuffers(1, &map_renderbuffer);
	glDeleteFramebuffers(1, &map_framebuffer);
}

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
		display_string(buffer, length, x + (FIELD_SIZE - (length * 10)) / 2, y + FIELD_SIZE, &font12, text);
	}
}

static void show_progress(unsigned current, unsigned total, unsigned x, unsigned y, unsigned width, unsigned height)
{
	// Progress is visualized as a sector with length proportional to the remaining time.

	if (current)
	{
		double progress = (double)current / total;
		double angle = progress * 2 * M_PI;

		glColor4ubv(display_colors[Progress]);

		glBegin(GL_POLYGON);

		glVertex2f(x + width / 2, y + height / 2);
		switch ((unsigned)(progress * 8))
		{
		case 0:
		case 7:
			glVertex2f(x + width, y + (1 - sin(angle)) * height / 2);
			break;

		case 1:
		case 2:
			glVertex2f(x + (1 + cos(angle)) * width / 2, y);
			break;

		case 3:
		case 4:
			glVertex2f(x, y + (1 - sin(angle)) * height / 2);
			break;

		case 5:
		case 6:
			glVertex2f(x + (1 + cos(angle)) * width / 2, y + height);
			break;
		}
		switch ((unsigned)(progress * 8))
		{
		case 0:
			glVertex2f(x + width, y);
		case 1:
		case 2:
			glVertex2f(x, y);
		case 3:
		case 4:
			glVertex2f(x, y + height);
		case 5:
		case 6:
			glVertex2f(x + width, y + height);
		}
		glVertex2f(x + width, y + height / 2);

		glEnd();
	}
	else fill_rectangle(x, y, width, height, Progress);
}

// TODO write this better
static int if_animation(const struct player *restrict players, const struct battle *restrict battle, double progress)
{
	int finished = 1;

	size_t x, y;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Display panel background pattern.
	display_image(&image_terrain[0], BATTLE_X - 8, BATTLE_Y - 8, BATTLEFIELD_WIDTH * FIELD_SIZE + 16, BATTLEFIELD_HEIGHT * FIELD_SIZE + 16);

	// Battlefield

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

	size_t p;
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
	gettimeofday(&start, 0);
	do
	{
		gettimeofday(&now, 0);
		progress = timediff(&now, &start) / (ANIMATION_DURATION * 1000000.0);
		if (if_animation(game->players, battle, progress))
			break;
	} while (progress < 1.0);
}

static void if_battlefield(unsigned char player, const struct game *game)
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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

	glFlush();
	glXSwapBuffers(display, drawable);
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

	display_string(buffer, end - buffer, x, y, &font12, White);

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

	display_string(buffer, end - buffer, x, y, &font12, White);
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

	glFlush();
	glXSwapBuffers(display, drawable);

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

static void show_resource(const struct image *restrict image, int treasury, int income, int expense, unsigned y)
{
	unsigned x;
	char buffer[32], *end; // TODO make sure this is enough

	image_draw(image, PANEL_X, y);
	end = format_uint(buffer, treasury, 10);

	x = PANEL_X + image->width;
	x += display_string(buffer, end - buffer, x, y, &font12, Black);
	if (income)
	{
		end = format_sint(buffer, income);
		x += display_string(buffer, end - buffer, x, y, &font12, Ally);
	}
	if (expense)
	{
		end = format_sint(buffer, -expense);
		x += display_string(buffer, end - buffer, x, y, &font12, Enemy);
	}
}

static void tooltip_cost(const char *restrict name, size_t name_length, const struct resources *restrict cost, unsigned time)
{
	char buffer[16];
	size_t length;

	unsigned offset = 120;

	display_string(name, name_length, TOOLTIP_X, TOOLTIP_Y, &font12, White);

	if (cost->gold)
	{
		image_draw(&image_gold, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, cost->gold, 10) - (uint8_t *)buffer;
		display_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->food)
	{
		image_draw(&image_food, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, cost->food, 10) - (uint8_t *)buffer;
		display_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->wood)
	{
		image_draw(&image_wood, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, cost->wood, 10) - (uint8_t *)buffer;
		display_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->stone)
	{
		image_draw(&image_stone, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, cost->stone, 10) - (uint8_t *)buffer;
		display_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->iron)
	{
		image_draw(&image_iron, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, cost->iron, 10) - (uint8_t *)buffer;
		display_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}

	if (time)
	{
		image_draw(&image_time, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, time, 10) - (uint8_t *)buffer;
		display_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
}

static inline void show_flag(unsigned x, unsigned y, unsigned player)
{
	fill_rectangle(x + 4, y + 4, 24, 12, Player + player);
	image_draw(&image_flag, x, y);
}

static inline void show_flag_small(unsigned x, unsigned y, unsigned player)
{
	fill_rectangle(x + 2, y + 2, 12, 6, Player + player);
	image_draw(&image_flag_small, x, y);
}

static void if_map_region(const struct region *region, const struct state_map *state, const struct game *game)
{
	unsigned state_alliance = game->players[state->player].alliance;
	int siege = (region->owner != region->garrison.owner);

	const struct troop *troop;
	size_t i;

	show_flag(PANEL_X, PANEL_Y, region->owner);

	// Display the troops at the selected region.
	if ((game->players[region->owner].alliance == state_alliance) || (game->players[region->garrison.owner].alliance == state_alliance))
	{
		enum object object;
		size_t offset;

		unsigned char self_count = 0, other_count = 0;

		// Display building information.
		// Construct button is displayed if the following conditions are satisfied:
		// * current player owns the region
		// * building requirements are satisfied
		// * there is no siege
		for(i = 0; i < buildings_count; ++i)
		{
			struct point position = if_position(Building, i);
			if (region_built(region, i)) image_draw(image_buildings + i, position.x, position.y);
			else if ((state->player == region->owner) && region_building_available(region, buildings[i]) && !siege) image_draw(image_buildings_gray + i, position.x, position.y);
		}

		// Display troops in the region.
		for(troop = region->troops; troop; troop = troop->_next)
		{
			size_t x;
			enum color color_text;
			int movement = 0;

			if (troop->owner == state->player)
			{
				if (!self_count) fill_rectangle(PANEL_X, object_group[TroopSelf].top - 2, PANEL_WIDTH, 2 + object_group[TroopSelf].height + 12 + 2, Self);
				x = self_count++;
				object = TroopSelf;
				offset = state->self_offset;
				color_text = Black;

				movement = (troop->move != troop->location);
			}
			else if (game->players[troop->owner].alliance == state_alliance)
			{
				if (!other_count) fill_rectangle(PANEL_X, object_group[TroopOther].top - 2, PANEL_WIDTH, 2 + object_group[TroopOther].height + 12 + 2, Ally);
				x = other_count++;
				object = TroopOther;
				offset = state->other_offset;
				color_text = White;
			}
			else if (region->garrison.owner == state->player)
			{
				if (!other_count) fill_rectangle(PANEL_X, object_group[TroopOther].top - 2, PANEL_WIDTH, 2 + object_group[TroopOther].height + 12 + 2, Enemy);
				x = other_count++;
				object = TroopOther;
				offset = state->other_offset;
				color_text = White;
			}

			// Draw troops that are visible on the screen.
			if ((x >= offset) && (x < offset + TROOPS_VISIBLE))
			{
				struct point position = if_position(object, x - offset);
				display_troop(troop->unit->index, position.x, position.y, Player + troop->owner, color_text, troop->count);
				if (movement) image_draw(&image_movement, position.x, position.y);
				if (troop == state->troop) draw_rectangle(position.x - 1, position.y - 1, object_group[object].width + 2, object_group[object].height + 2, White);
			}

			// Draw the destination of each moving troop owned by current player.
			if ((troop->owner == state->player) && (!state->troop || (troop == state->troop)) && (troop->move->index != state->region))
			{
				struct point p0, p1;
				if (polygons_border(troop->location->location, troop->move->location, &p0, &p1)) // TODO this is slow; don't do it every time
				{
					// Take the points p0 and p1 as endpoints of the diagonal of the square and find the endpoints of the other diagonal.
					// http://stackoverflow.com/questions/27829304/calculate-bisector-segment-coordinates

					int x = p1.x - p0.x;
					int y = p1.y - p0.y;

					double xm = p0.x + x / 2.0;
					double ym = p0.y + y / 2.0;

					double length = sqrt(y * y + x * x);
					double dx = ARROW_LENGTH * y / (2 * length);
					double dy = ARROW_LENGTH * x / (2 * length);

					struct point from = {xm + dx, ym - dy};
					struct point to = {xm - dx, ym + dy};

					display_arrow(from, to, MAP_X, MAP_Y, Self); // TODO change color
				}
				else ; // TODO buggy map
			}
		}

		// Display scroll buttons.
		if (state->self_offset) image_draw(&image_scroll_left, PANEL_X, object_group[TroopSelf].top);
		if ((self_count - state->self_offset) > TROOPS_VISIBLE) image_draw(&image_scroll_right, object_group[TroopSelf].span_x + object_group[TroopSelf].padding, object_group[TroopSelf].top);
		if (state->other_offset) image_draw(&image_scroll_left, PANEL_X, object_group[TroopOther].top);
		if ((other_count - state->other_offset) > TROOPS_VISIBLE) image_draw(&image_scroll_right, object_group[TroopOther].span_x + object_group[TroopOther].padding, object_group[TroopOther].top);

		// Display garrison and garrison troops.
		{
			const struct garrison_info *restrict garrison = garrison_info(region);
			if (garrison)
			{
				image_draw(&image_garrison[garrison->index], GARRISON_X, GARRISON_Y);

				fill_rectangle(GARRISON_X + 4, GARRISON_Y - GARRISON_MARGIN + 4, 24, 12, Player + region->garrison.owner);
				image_draw(&image_flag, GARRISON_X, GARRISON_Y - GARRISON_MARGIN);

				i = 0;
				for(troop = region->garrison.troops; troop; troop = troop->_next)
				{
					struct point position = if_position(TroopGarrison, i);
					display_troop(troop->unit->index, position.x, position.y, Player + troop->owner, Black, troop->count);
					i += 1;
				}

				// If the garrison is under siege, display siege information.
				if (siege)
				{
					unsigned provisions = garrison->provisions - region->garrison.siege;
					for(i = 0; i < provisions; ++i)
						image_draw(&image_food, object_group[TroopGarrison].right + 9, object_group[TroopGarrison].bottom - (i + 1) * image_food.height);
				}
			}

			if (region->garrison.assault && (state->player == region->owner)) // current player is doing an assault
			{
				i = 0;
				for(troop = region->garrison.troops; troop; troop = troop->_next)
				{
					struct point position = if_position(TroopGarrison, i);
					image_draw(&image_assault, position.x, position.y);
					i += 1;
				}
			}
		}
	}
	else if (state->regions_visible[region->index])
	{
		// Display an estimation of the number troops in the region.
		// Don't include garrison troops.

		// TODO fix this

		unsigned count = 0;
		char buffer[16], *end; // TODO make sure this is enough

		// TODO exclude troops going out of the garrison

		for(troop = region->troops; troop; troop = troop->_next)
			count += troop->count;

		count = ((count + 5) / 10) * 10;
		end = format_uint(buffer, count, 10);

		display_string(S("estimated troops:"), PANEL_X + 2, PANEL_Y + 32, &font12, Black);
		display_string(buffer, end - buffer, PANEL_X + 140, PANEL_Y + 32, &font12, Black);
	}

	if ((state->player == region->owner) && !siege)
	{
		if (region->construct >= 0)
		{
			struct point position = if_position(Building, region->construct);
			show_progress(region->build_progress, buildings[region->construct].time, position.x, position.y, object_group[Building].width, object_group[Building].height);
			image_draw(&image_construction, position.x, position.y);
		}

		display_string(S("train:"), PANEL_X + 2, object_group[Dismiss].top + (object_group[Dismiss].height - font12.height) / 2, &font12, Black);

		// Display train queue.
		size_t index;
		for(index = 0; index < TRAIN_QUEUE; ++index)
		{
			struct point position = if_position(Dismiss, index);
			if (region->train[index])
			{
				display_troop(region->train[index]->index, position.x, position.y, Player, 0, 0);
				show_progress((index ? 0 : region->train_progress), region->train[0]->time, position.x, position.y, object_group[Dismiss].width, object_group[Dismiss].height);
			}
			else fill_rectangle(position.x, position.y, object_group[Dismiss].width, object_group[Dismiss].height, Black);
		}

		// Display units available for training.
		for(index = 0; index < UNITS_COUNT; ++index)
		{
			if (!region_unit_available(region, UNITS[index])) continue;

			struct point position = if_position(Train, index);
			display_troop(index, position.x, position.y, Player, 0, 0);
		}
	}

	// Display tooltip for the hovered object.
	switch (state->hover_object)
	{
	case HOVER_UNIT:
		{
			const struct unit *unit = UNITS + state->hover.unit;
			tooltip_cost(unit->name, unit->name_length, &unit->cost, unit->time);
		}
		break;
	case HOVER_BUILDING:
		if (region_built(region, state->hover.building))
		{
			const struct building *building = buildings + state->hover.building;
			const struct resources none = {0};
			tooltip_cost(building->name, building->name_length, &none, 0);
		}
		else
		{
			const struct building *building = buildings + state->hover.building;
			tooltip_cost(building->name, building->name_length, &building->cost, building->time);
		}
		break;
	}
}

void if_map(const void *argument, const struct game *game)
{
	const struct state_map *state = argument;

	size_t i, j;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Display current player's color.
	// TODO use darker color in the center
	draw_rectangle(PANEL_X - 4, PANEL_Y - 4, PANEL_WIDTH + 8, PANEL_HEIGHT + 8, Player + state->player);
	draw_rectangle(PANEL_X - 3, PANEL_Y - 3, PANEL_WIDTH + 6, PANEL_HEIGHT + 6, Player + state->player);
	draw_rectangle(PANEL_X - 2, PANEL_Y - 2, PANEL_WIDTH + 4, PANEL_HEIGHT + 4, Player + state->player);

	// Display panel background pattern.
	display_image(&image_panel, PANEL_X, PANEL_Y, PANEL_WIDTH, PANEL_HEIGHT);

	// TODO remove this color display box
	/*for(i = 0; i < PLAYERS_LIMIT; ++i)
		fill_rectangle(PANEL_X + (i % 4) * 32, PANEL_Y + 300 + (i / 4) * 32, 32, 32, Player + i);*/

	// Map

	struct resources income = {0}, expenses = {0};
	const struct troop *troop;

	// TODO place income logic at a single place (now it's here and in main.c)

	for(i = 0; i < game->regions_count; ++i)
	{
		const struct region *restrict region = game->regions + i;

		// Fill each region with the color of its owner (or the color indicating unexplored).
		if (state->regions_visible[i]) glColor4ubv(display_colors[Player + region->owner]);
		else glColor4ubv(display_colors[Unexplored]);
		display_polygon(region->location, MAP_X, MAP_Y);

		// Remember income and expenses.
		if (region->owner == state->player) region_income(region, &income);
		for(troop = region->troops; troop; troop = troop->_next)
			if (troop->owner == state->player)
			{
				if (region->owner != region->garrison.owner)
				{
					struct resources expense;
					resource_multiply(&expense, &troop->unit->expense, 2);
					resource_add(&expenses, &expense);
				}
				else resource_add(&expenses, &troop->unit->expense);
			}
	}

	// Draw region borders.
	for(i = 0; i < game->regions_count; ++i)
	{
		glColor4ubv(display_colors[Black]);
		glBegin(GL_LINE_STRIP);
		for(j = 0; j < game->regions[i].location->vertices_count; ++j)
			glVertex2f(MAP_X + game->regions[i].location->points[j].x, MAP_Y + game->regions[i].location->points[j].y);
		glEnd();
	}

	for(i = 0; i < game->regions_count; ++i)
	{
		if (!state->regions_visible[i]) continue;

		// Display garrison if built.
		const struct region *region = game->regions + i;
		const struct garrison_info *restrict garrison = garrison_info(region);
		if (garrison)
		{
			const struct image *restrict image = &image_map_garrison[garrison->index];
			unsigned location_x = MAP_X + region->location_garrison.x - image->width / 2;
			unsigned location_y = MAP_Y + region->location_garrison.y - image->height / 2;
			display_image(image, location_x, location_y, image->width, image->height);
			show_flag_small(MAP_X + region->location_garrison.x, location_y - image_flag_small.height + 8, region->garrison.owner);
		}
	}

	if (state->region >= 0)
	{
		const struct region *region = game->regions + state->region;

		// Show the name of the selected region.
		display_string(region->name, region->name_length, PANEL_X + image_flag.width + MARGIN, PANEL_Y + (image_flag.height - font12.height) / 2, &font12, Black);

		if (state->regions_visible[state->region]) if_map_region(region, state, game);
	}

	// treasury
	struct resources *treasury = &game->players[state->player].treasury;
	show_resource(&image_gold, treasury->gold, income.gold, expenses.gold, RESOURCE_GOLD);
	show_resource(&image_food, treasury->food, income.food, expenses.food, RESOURCE_FOOD);
	show_resource(&image_wood, treasury->wood, income.wood, expenses.wood, RESOURCE_WOOD);
	show_resource(&image_stone, treasury->stone, income.stone, expenses.stone, RESOURCE_STONE);
	show_resource(&image_iron, treasury->iron, income.iron, expenses.iron, RESOURCE_IRON);

	glFlush();
	glXSwapBuffers(display, drawable);
}

void if_set(struct battle *b)
{
	battle = b;
}
