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

#include <sys/time.h>
#include <unistd.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>
#include <GL/glext.h>

#include "format.h"
#include "game.h"
#include "draw.h"
#include "resources.h"
#include "map.h"
#include "pathfinding.h"
#include "interface.h"
#include "display_common.h"
#include "image.h"
#include "input_map.h"
#include "display_battle.h"

#define S(s) (s), sizeof(s) - 1

#define RESOURCE_GOLD 660
#define RESOURCE_FOOD 680
#define RESOURCE_WOOD 700
#define RESOURCE_IRON 720
#define RESOURCE_STONE 740

//#define HEALTH_BAR 64
#define TROOPS_BAR_MARGIN 2

#define TROOPS_BAR_WIDTH 4
#define TROOPS_BAR_HEIGHT 48

// TODO compatibility with OpenGL 2.1 (necessary in MacOS X)
#define glGenFramebuffers(...) glGenFramebuffersEXT(__VA_ARGS__)
#define glGenRenderbuffers(...) glGenRenderbuffersEXT(__VA_ARGS__)
#define glBindFramebuffer(...) glBindFramebufferEXT(__VA_ARGS__)
#define glBindRenderbuffer(...) glBindRenderbufferEXT(__VA_ARGS__)
#define glRenderbufferStorage(...) glRenderbufferStorageEXT(__VA_ARGS__)
#define glFramebufferRenderbuffer(...) glFramebufferRenderbufferEXT(__VA_ARGS__)

#define ARROW_LENGTH 50

static GLuint map_framebuffer;

static GLuint map_renderbuffer;

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
		unsigned char color[4] = {255, i / 256, i % 256, 255};
		fill_polygon(game->regions[i].location, 0, 0, color, 1.0);
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
	else fill_rectangle(x, y, width, height, display_colors[Progress]);
}

static void show_resource(const struct image *restrict image, int treasury, int income, unsigned x, unsigned y)
{
	char buffer[32], *end; // TODO make sure this is enough

	image_draw(image, x, y);
	x += image->width;

	// TODO draw the string just once
	end = format_int(buffer, treasury, 10);
	x += draw_string(buffer, end - buffer, x, y, &font12, Black);
	if (income)
	{
		end = format_sint(buffer, income);
		x += draw_string(buffer, end - buffer, x, y, &font12, ((income > 0) ? Self : Enemy));
	}
}

static void tooltip_cost(const char *restrict name, size_t name_length, const struct resources *restrict cost, unsigned time)
{
	char buffer[16];
	size_t length;

	unsigned offset = 120;

	draw_string(name, name_length, TOOLTIP_X, TOOLTIP_Y, &font12, White);

	if (cost->gold)
	{
		image_draw(&image_gold, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, -cost->gold, 10) - (uint8_t *)buffer;
		draw_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->food)
	{
		image_draw(&image_food, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, -cost->food, 10) - (uint8_t *)buffer;
		draw_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->wood)
	{
		image_draw(&image_wood, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, -cost->wood, 10) - (uint8_t *)buffer;
		draw_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->stone)
	{
		image_draw(&image_stone, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, -cost->stone, 10) - (uint8_t *)buffer;
		draw_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
	if (cost->iron)
	{
		image_draw(&image_iron, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, -cost->iron, 10) - (uint8_t *)buffer;
		draw_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}

	if (time)
	{
		image_draw(&image_time, TOOLTIP_X + offset, TOOLTIP_Y);
		offset += 16;

		length = format_uint(buffer, time, 10) - (uint8_t *)buffer;
		draw_string(buffer, length, TOOLTIP_X + offset, TOOLTIP_Y, &font12, White);
		offset += 40;
	}
}

static void if_map_troops(unsigned x, unsigned y, unsigned count_self, unsigned count_ally, unsigned count_enemy)
{
	// Calculate the height of each bar and draw it.

	// assert(TROOPS_BAR_HEIGHT == image_map_village.height);

	// Make sure the troops bar doesn't get out of the screen.
	if (y < TROOPS_BAR_HEIGHT + 2) y = TROOPS_BAR_HEIGHT + 2;

	if (count_self = count_round(count_self))
	{
		if (count_self > TROOPS_BAR_HEIGHT)
		{
			fill_rectangle(x - 1, y - TROOPS_BAR_HEIGHT - 2, TROOPS_BAR_WIDTH + 2, 1, display_colors[Self]);
			count_self = TROOPS_BAR_HEIGHT;
		}
		fill_rectangle(x, y - count_self, TROOPS_BAR_WIDTH, count_self, display_colors[Self]);
		draw_rectangle(x - 1, y - count_self - 1, TROOPS_BAR_WIDTH + 2, count_self + 2, display_colors[Black]);

		x += TROOPS_BAR_WIDTH + 1;
	}

	if (count_ally = count_round(count_ally))
	{
		if (count_ally > TROOPS_BAR_HEIGHT)
		{
			fill_rectangle(x - 1, y - TROOPS_BAR_HEIGHT - 2, TROOPS_BAR_WIDTH + 2, 1, display_colors[Ally]);
			count_ally = TROOPS_BAR_HEIGHT;
		}
		fill_rectangle(x, y - count_ally, TROOPS_BAR_WIDTH, count_ally, display_colors[Ally]);
		draw_rectangle(x - 1, y - count_ally - 1, TROOPS_BAR_WIDTH + 2, count_ally + 2, display_colors[Black]);

		x += TROOPS_BAR_WIDTH + 1;
	}

	if (count_enemy = count_round(count_enemy))
	{
		if (count_enemy > TROOPS_BAR_HEIGHT)
		{
			fill_rectangle(x - 1, y - TROOPS_BAR_HEIGHT - 2, TROOPS_BAR_WIDTH + 2, 1, display_colors[Enemy]);
			count_enemy = TROOPS_BAR_HEIGHT;
		}
		fill_rectangle(x, y - count_enemy, TROOPS_BAR_WIDTH, count_enemy, display_colors[Enemy]);
		draw_rectangle(x - 1, y - count_enemy - 1, TROOPS_BAR_WIDTH + 2, count_enemy + 2, display_colors[Black]);

		x += TROOPS_BAR_WIDTH + 1;
	}
}

static void display_troop_destination(struct point p0, struct point p1)
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

static void display_economy_resource(struct point position, unsigned workers, unsigned workers_unused, const struct image *restrict image, int resource)
{
	unsigned offset;
	char buffer[32], *end; // TODO make sure this is enough

	offset = 100 - (workers + workers_unused);
	fill_rectangle(position.x, position.y + offset, object_group[Workers].width, object_group[Workers].height - offset, display_colors[Black]);
	fill_rectangle(position.x + 1, object_group[Workers].bottom - 1 - workers, object_group[Workers].width - 2, workers, display_colors[Self]);
	end = format_uint(buffer, workers, 10); // TODO divide this by 100
	draw_string(buffer, end - buffer, position.x, object_group[Workers].bottom + PROFIT_PADDING, &font12, Black);
	show_resource(image, resource, 0, position.x, PROFIT_Y);
}

static void display_economy(const struct state_map *restrict state, const struct region *restrict region)
{
	char buffer[32], *end; // TODO make sure this is enough
	unsigned workers_unused = 100 - (region->workers.food + region->workers.wood + region->workers.iron + region->workers.stone);

	end = format_bytes(buffer, S("population: "));
	end = format_uint(end, region->population, 10);
	draw_string(buffer, end - buffer, POPULATION_X, POPULATION_Y, &font12, Black);

	if (state->workers_food)
		display_economy_resource(if_position(Workers, 0), region->workers.food, workers_unused, &image_food, state->region_income.food);
	if (state->workers_wood)
		display_economy_resource(if_position(Workers, 1), region->workers.wood, workers_unused, &image_wood, state->region_income.wood);
	if (state->workers_stone)
		display_economy_resource(if_position(Workers, 2), region->workers.stone, workers_unused, &image_stone, state->region_income.stone);
	if (state->workers_iron)
		display_economy_resource(if_position(Workers, 3), region->workers.iron, workers_unused, &image_iron, state->region_income.iron);

	draw_string(S("tax savings:"), SAVINGS_X, SAVINGS_Y, &font12, Black);
	show_resource(&image_gold, state->region_income.gold, 0, SAVINGS_X + 90, SAVINGS_Y); // TODO remove this 90
}

static void if_map_region(const struct region *region, const struct state_map *state, const struct game *game)
{
	int siege = (region->owner != region->garrison.owner);

	const struct troop *troop;
	size_t i;

	if (game->players[region->owner].type != Neutral) show_flag(PANEL_X, PANEL_Y, region->owner);

	if (!state->economy)
	{
		if (allies(game, region->owner, state->player) || allies(game, region->garrison.owner, state->player))
		{
			enum object object;
			size_t offset;
			size_t x;

			unsigned char self_count = 0, other_count = 0;

			// Display building information.
			// Construct button is displayed if the following conditions are satisfied:
			// * current player owns the region
			// * the building is not built
			// * building requirements are satisfied
			// * there is no siege
			for(i = 0; i < BUILDINGS_COUNT; ++i)
			{
				struct point position = if_position(Building, i);
				if (region_built(region, i) && !(region->built & BUILDINGS[i].conflicts))
				{
					draw_rectangle(position.x - 1, position.y - 1, image_buildings[i].width + 2, image_buildings[i].height + 2, display_colors[Black]);
					image_draw(image_buildings + i, position.x, position.y);
				}
				else if ((state->player == region->owner) && region_building_available(region, BUILDINGS + i) && !siege)
				{
					draw_rectangle(position.x - 1, position.y - 1, image_buildings[i].width + 2, image_buildings[i].height + 2, display_colors[Black]);
					image_draw(image_buildings_gray + i, position.x, position.y);
				}
			}

			// Display troops in the region.
			for(troop = region->troops; troop; troop = troop->_next)
			{
				enum color color_text;
				const struct image *restrict image_action = 0;

				if (troop->owner == state->player)
				{
					if (troop->dismiss)
					{
						image_action = &image_dismiss;
					}
					else if (troop->move == LOCATION_GARRISON)
					{
						if (troop->owner == region->garrison.owner)
							continue; // troop is in garrison
						image_action = &image_assault;
					}
					else if (troop->move != region)
					{
						image_action = &image_movement;

						// Display troop destination if necessary.
						if (!state->troop || (troop == state->troop))
						{
							struct point p0, p1;
							if (polygons_border(region->location, troop->move->location, &p0, &p1)) // TODO this is slow; don't do it every time
							{
								display_troop_destination(p0, p1);
							}
							else
							{
								// TODO do something better
								write(2, S("Neighboring regions have no common border\n")); // world bug
							}
						}
					}

					if (!self_count) fill_rectangle(PANEL_X, object_group[TroopSelf].top - 2, PANEL_WIDTH, 2 + object_group[TroopSelf].height + 12 + 2, display_colors[Self]);
					x = self_count++;
					object = TroopSelf;
					offset = state->self_offset;
					color_text = Black;
				}
				else if (allies(game, troop->owner, state->player))
				{
					if (troop->location == LOCATION_GARRISON) continue;

					if (!other_count) fill_rectangle(PANEL_X, object_group[TroopOther].top - 2, PANEL_WIDTH, 2 + object_group[TroopOther].height + 12 + 2, display_colors[Ally]);
					x = other_count++;
					object = TroopOther;
					offset = state->other_offset;
					color_text = White;
				}
				else
				{
					if (troop->location == LOCATION_GARRISON) continue;

					if (!other_count) fill_rectangle(PANEL_X, object_group[TroopOther].top - 2, PANEL_WIDTH, 2 + object_group[TroopOther].height + 12 + 2, display_colors[Enemy]);
					x = other_count++;
					object = TroopOther;
					offset = state->other_offset;
					color_text = White;
				}

				// If the troop is visible on the screen, draw it.
				if ((x >= offset) && (x < offset + TROOPS_VISIBLE))
				{
					struct point position = if_position(object, x - offset);
					fill_rectangle(position.x, position.y, object_group[object].width, object_group[object].height, display_colors[Black]);
					display_troop(troop->unit->index, position.x, position.y, Player + troop->owner, color_text, troop->count);
					if (image_action) image_draw(image_action, position.x, position.y); // draw action indicator for the troop
					if (troop == state->troop) draw_rectangle(position.x - 1, position.y - 1, object_group[object].width + 2, object_group[object].height + 2, display_colors[White]);
				}
			}

			// Display scroll buttons.
			if (state->self_offset) image_draw(&image_scroll_left, PANEL_X, object_group[TroopSelf].top);
			if ((self_count - state->self_offset) > TROOPS_VISIBLE) image_draw(&image_scroll_right, object_group[TroopSelf].right + 1, object_group[TroopSelf].top);
			if (state->other_offset) image_draw(&image_scroll_left, PANEL_X, object_group[TroopOther].top);
			if ((other_count - state->other_offset) > TROOPS_VISIBLE) image_draw(&image_scroll_right, object_group[TroopOther].right + 1, object_group[TroopOther].top);

			// Display garrison and garrison troops.
			const struct garrison_info *restrict garrison = garrison_info(region);
			if (garrison)
			{
				image_draw(&image_garrisons[garrison->index], GARRISON_X, GARRISON_Y);
				if (game->players[region->garrison.owner].type != Neutral) show_flag(GARRISON_X, GARRISON_Y - GARRISON_MARGIN, region->garrison.owner);

				if (allies(game, region->garrison.owner, state->player))
				{
					i = 0;
					for(troop = region->troops; troop; troop = troop->_next)
					{
						const struct region *restrict location = ((troop->owner == state->player) ? troop->move : troop->location);
						if ((location != LOCATION_GARRISON) || (troop->owner != region->garrison.owner))
							continue;

						struct point position = if_position(TroopGarrison, i);
						fill_rectangle(position.x, position.y, object_group[TroopGarrison].width, object_group[TroopGarrison].height, display_colors[Black]);
						display_troop(troop->unit->index, position.x, position.y, Player + troop->owner, Black, troop->count);
						if (troop == state->troop) draw_rectangle(position.x - 1, position.y - 1, object_group[TroopGarrison].width + 2, object_group[TroopGarrison].height + 2, display_colors[White]);

						i += 1;
					}
				}
				else
				{
					// Display an estimate of the number of troops in the garrison.

					char buffer[32], *end; // TODO make sure this is enough

					unsigned count = 0;
					for(troop = region->troops; troop; troop = troop->_next)
						if (troop->location == LOCATION_GARRISON)
							count += troop->count;
					count = count_round(count) * COUNT_ROUND_PRECISION;

					end = format_bytes(buffer, S("about "));
					end = format_uint(end, count, 10);
					end = format_bytes(end, S(" troops"));

					draw_string(buffer, end - buffer, object_group[TroopGarrison].left, object_group[TroopGarrison].top, &font12, Black);
				}

				// If the garrison is under siege, display the remaining provisions.
				if (siege)
				{
					unsigned provisions = garrison->provisions - region->garrison.siege;
					for(i = 0; i < provisions; ++i)
						image_draw(&image_food, object_group[TroopGarrison].right + 9, object_group[TroopGarrison].bottom - (i + 1) * image_food.height);
				}
			}
		}
		else
		{
			// Display an estimate of the number of troops in the region.

			char buffer[32], *end; // TODO make sure this is enough

			unsigned count = 0;
			for(troop = region->troops; troop; troop = troop->_next)
				if (troop->location != LOCATION_GARRISON)
					count += troop->count;
			count = count_round(count) * COUNT_ROUND_PRECISION;

			end = format_bytes(buffer, S("about "));
			end = format_uint(end, count, 10);
			end = format_bytes(end, S(" troops"));

			draw_string(buffer, end - buffer, object_group[TroopSelf].left, object_group[TroopSelf].top, &font12, Black);
		}
	}

	if ((state->player == region->owner) && !siege)
	{
		image_draw(&image_economy, ECONOMY_X, ECONOMY_Y);

		if (state->economy)
		{
			display_economy(state, region);
		}
		else
		{
			if (region->construct >= 0)
			{
				struct point position = if_position(Building, region->construct);
				show_progress(region->build_progress, BUILDINGS[region->construct].time, position.x, position.y, object_group[Building].width, object_group[Building].height);
				image_draw(&image_construction, position.x, position.y);
			}

			draw_string(S("train:"), PANEL_X + 2, object_group[Dismiss].top + (object_group[Dismiss].height - font12.height) / 2, &font12, Black);

			// Display train queue.
			size_t index;
			for(index = 0; index < TRAIN_QUEUE; ++index)
			{
				struct point position = if_position(Dismiss, index);
				fill_rectangle(position.x, position.y, object_group[Dismiss].width, object_group[Dismiss].height, display_colors[Black]);
				if (region->train[index])
				{
					display_troop(region->train[index]->index, position.x, position.y, Player, 0, 0);
					show_progress((index ? 0 : region->train_progress), region->train[0]->time, position.x, position.y, object_group[Dismiss].width, object_group[Dismiss].height);
				}
			}

			// Display units available for training.
			for(index = 0; index < UNITS_COUNT; ++index)
			{
				if (!region_unit_available(region, UNITS[index])) continue;

				struct point position = if_position(Train, index);
				fill_rectangle(position.x, position.y, object_group[Train].width, object_group[Train].height, display_colors[Black]);
				display_troop(index, position.x, position.y, Player, 0, 0);
			}
		}
	}

	// Display tooltip for the hovered object.
	if (!state->economy) switch (state->hover_object)
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
			const struct building *building = BUILDINGS + state->hover.building;
			const struct resources none = {0};
			tooltip_cost(building->name, building->name_length, &none, 0);
		}
		else
		{
			const struct building *building = BUILDINGS + state->hover.building;
			tooltip_cost(building->name, building->name_length, &building->cost, building->time);
		}
		break;
	}
}

static inline int in_garrison(const struct troop *restrict troop, const struct region *restrict region, unsigned player)
{
	if (troop->owner == player)
		return ((troop->move == LOCATION_GARRISON) && (troop->owner == region->garrison.owner));
	else
		return (troop->location == LOCATION_GARRISON);
}

void if_map(const void *argument, const struct game *game)
{
	const struct state_map *state = argument;

	size_t i;

	// Display current player's color.
	// TODO use darker color in the center
	draw_rectangle(PANEL_X - 4, PANEL_Y - 4, PANEL_WIDTH + 8, PANEL_HEIGHT + 8, display_colors[Player + state->player]);
	draw_rectangle(PANEL_X - 3, PANEL_Y - 3, PANEL_WIDTH + 6, PANEL_HEIGHT + 6, display_colors[Player + state->player]);
	draw_rectangle(PANEL_X - 2, PANEL_Y - 2, PANEL_WIDTH + 4, PANEL_HEIGHT + 4, display_colors[Player + state->player]);

	// Display panel background pattern.
	display_image(&image_panel, PANEL_X, PANEL_Y, PANEL_WIDTH, PANEL_HEIGHT);

	// TODO display year and month
	// {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"}
	// game->turn / 12;
	// game->turn % 12;

	// TODO remove this color display box
	/*for(i = 0; i < PLAYERS_LIMIT; ++i)
		fill_rectangle(PANEL_X + (i % 4) * 32, PANEL_Y + 300 + (i / 4) * 32, 32, 32, Player + i);*/

	// Map

	struct resources income = {0};
	const struct troop *troop;

	// TODO place income logic at a single place (now it's here and in main.c)

	for(i = 0; i < game->regions_count; ++i)
	{
		const struct region *restrict region = game->regions + i;

		// Fill each region with the color of its owner (or the color indicating unexplored).
		enum color color = (state->regions_visible[i] ? (Player + region->owner) : Unexplored);
		fill_polygon(region->location, MAP_X, MAP_Y, display_colors[color], 1.0);

		// Remember income and expenses.
		if (region->owner == state->player) region_income(region, &income);
		for(troop = region->troops; troop; troop = troop->_next)
		{
			if (troop->owner != state->player) continue;
			if (troop->location == LOCATION_GARRISON) continue;

			if (region->owner != region->garrison.owner) // Troops expenses are covered by another region. Double expenses.
			{
				struct resources expense;
				resource_multiply(&expense, &troop->unit->support, 2);
				resource_add(&income, &expense);
			}
			else resource_add(&income, &troop->unit->support);
		}
	}

	// Draw region borders.
	for(i = 0; i < game->regions_count; ++i)
		draw_polygon(game->regions[i].location, MAP_X, MAP_Y, display_colors[Black], 1.0);

	for(i = 0; i < game->regions_count; ++i)
	{
		if (!state->regions_visible[i]) continue;

		const struct region *region = game->regions + i;

		unsigned count_self, count_allies, count_enemies;

		// Display garrison if built.
		const struct garrison_info *restrict garrison = garrison_info(region);
		if (garrison)
		{
			const struct image *restrict image = &image_map_garrison[garrison->index];
			unsigned location_x = MAP_X + region->location_garrison.x - image->width / 2;
			unsigned location_y = MAP_Y + region->location_garrison.y - image->height / 2;
			display_image(image, location_x, location_y, image->width, image->height);
			if (game->players[region->garrison.owner].type != Neutral) show_flag_small(MAP_X + region->location_garrison.x, location_y - image_flag_small.height + 10, region->garrison.owner);

			if (allies(game, region->owner, state->player) || allies(game, region->garrison.owner, state->player))
			{
				count_self = 0;
				count_allies = 0;
				count_enemies = 0;
				for(troop = region->troops; troop; troop = troop->_next)
				{
					if (!in_garrison(troop, region, state->player)) continue;

					if (troop->owner == state->player) count_self += troop->count;
					else if (allies(game, troop->owner, state->player)) count_allies += troop->count;
					else count_enemies += troop->count;
				}

				if_map_troops(location_x + image->width + TROOPS_BAR_MARGIN, location_y + image->height, count_self, count_allies, count_enemies);
			}
		}

		// Display village image.
		// TODO fix this; it requires bigger regions and grass as background
		// unsigned x, y;
		/*x = MAP_X + region->center.x - image_map_village.width;
		y = MAP_Y + region->center.y - image_map_village.height;
		display_image(&image_map_village, x, y, image_map_village.width, image_map_village.height);*/
		// TODO padding between village image and troops bar

		// Display a bar showing the number of troops in the region.
		// Don't include garrison troops.
		count_self = 0;
		count_allies = 0;
		count_enemies = 0;
		for(troop = region->troops; troop; troop = troop->_next)
		{
			if (in_garrison(troop, region, state->player)) continue;

			if (troop->owner == state->player) count_self += troop->count;
			else if (allies(game, troop->owner, state->player)) count_allies += troop->count;
			else count_enemies += troop->count;
		}
		if_map_troops(MAP_X + region->center.x, MAP_Y + region->center.y, count_self, count_allies, count_enemies);
	}

	if (state->region >= 0)
	{
		const struct region *region = game->regions + state->region;

		// Show the name of the selected region.
		draw_string(region->name, region->name_length, PANEL_X + image_flag.width + MARGIN, PANEL_Y + (image_flag.height - font12.height) / 2, &font12, Black);

		if (state->regions_visible[state->region]) if_map_region(region, state, game);
	}

	// treasury
	struct resources *treasury = &game->players[state->player].treasury;
	show_resource(&image_gold, treasury->gold, income.gold, PANEL_X, RESOURCE_GOLD);
	show_resource(&image_food, treasury->food, income.food, PANEL_X, RESOURCE_FOOD);
	show_resource(&image_wood, treasury->wood, income.wood, PANEL_X, RESOURCE_WOOD);
	show_resource(&image_stone, treasury->stone, income.stone, PANEL_X, RESOURCE_STONE);
	show_resource(&image_iron, treasury->iron, income.iron, PANEL_X, RESOURCE_IRON);

	show_button(S("Ready"), BUTTON_READY_X, BUTTON_READY_Y);
	show_button(S("Menu"), BUTTON_MENU_X, BUTTON_MENU_Y);
}
